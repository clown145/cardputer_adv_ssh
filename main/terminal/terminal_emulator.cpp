#include "terminal/terminal_emulator.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace adv {
namespace {
constexpr TerminalCell kBlank{};
constexpr uint8_t kCellBold = 1 << 0;
constexpr uint8_t kCellInverse = 1 << 1;
constexpr uint8_t kCellUnderline = 1 << 2;
constexpr uint8_t kCellDim = 1 << 3;
constexpr uint8_t kCellHidden = 1 << 4;
constexpr uint8_t kCellStrikethrough = 1 << 5;
constexpr uint8_t kCellContinuation = 1 << 6;
constexpr size_t kScrollbackMinFreeHeap = 48 * 1024;
constexpr size_t kScrollbackMinLargestBlock = 4 * 1024;

int clamp_int(int value, int low, int high)
{
    return std::max(low, std::min(value, high));
}

uint8_t rgb_to_ansi(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t max_channel = std::max(r, std::max(g, b));
    uint8_t min_channel = std::min(r, std::min(g, b));
    if (max_channel < 64) {
        return 0;
    }
    if (min_channel > 190) {
        return 15;
    }
    bool bright = max_channel > 170;
    if (max_channel - min_channel < 40) {
        return bright ? 7 : 8;
    }
    uint8_t color = 0;
    if (r > 100) {
        color |= 1;
    }
    if (g > 100) {
        color |= 2;
    }
    if (b > 100) {
        color |= 4;
    }
    return color + (bright ? 8 : 0);
}

uint8_t ansi256_to_ansi(uint8_t color)
{
    if (color < 16) {
        return color;
    }
    if (color >= 232) {
        return color >= 244 ? 7 : 8;
    }
    color -= 16;
    uint8_t r = color / 36;
    uint8_t g = (color / 6) % 6;
    uint8_t b = color % 6;
    auto expand = [](uint8_t value) -> uint8_t {
        return value == 0 ? 0 : 55 + value * 40;
    };
    return rgb_to_ansi(expand(r), expand(g), expand(b));
}

uint8_t vterm_color_to_ansi(VTermColor color, bool foreground)
{
    if (foreground && VTERM_COLOR_IS_DEFAULT_FG(&color)) {
        return 7;
    }
    if (!foreground && VTERM_COLOR_IS_DEFAULT_BG(&color)) {
        return 0;
    }
    if (VTERM_COLOR_IS_INDEXED(&color)) {
        return ansi256_to_ansi(color.indexed.idx);
    }
    if (VTERM_COLOR_IS_RGB(&color)) {
        return rgb_to_ansi(color.rgb.red, color.rgb.green, color.rgb.blue);
    }
    return foreground ? 7 : 0;
}

VTermKey to_vterm_key(TerminalInputKey key)
{
    switch (key) {
        case TerminalInputKey::kEnter:
            return VTERM_KEY_ENTER;
        case TerminalInputKey::kTab:
            return VTERM_KEY_TAB;
        case TerminalInputKey::kBackspace:
            return VTERM_KEY_BACKSPACE;
        case TerminalInputKey::kEscape:
            return VTERM_KEY_ESCAPE;
        case TerminalInputKey::kUp:
            return VTERM_KEY_UP;
        case TerminalInputKey::kDown:
            return VTERM_KEY_DOWN;
        case TerminalInputKey::kLeft:
            return VTERM_KEY_LEFT;
        case TerminalInputKey::kRight:
            return VTERM_KEY_RIGHT;
    }
    return VTERM_KEY_NONE;
}

void* allocate_scrollback_bytes(size_t bytes)
{
    if (bytes == 0) {
        return nullptr;
    }
#ifdef ESP_PLATFORM
    return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
#else
    return std::malloc(bytes);
#endif
}

void free_scrollback_bytes(void* ptr)
{
    if (ptr == nullptr) {
        return;
    }
#ifdef ESP_PLATFORM
    heap_caps_free(ptr);
#else
    std::free(ptr);
#endif
}

bool has_scrollback_heap_budget(size_t bytes)
{
    if (bytes == 0) {
        return true;
    }
#ifdef ESP_PLATFORM
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    size_t required_largest = std::max(bytes, kScrollbackMinLargestBlock);
    return free_heap > kScrollbackMinFreeHeap + bytes && largest_block >= required_largest;
#else
    (void)bytes;
    return true;
#endif
}
}  // namespace

TerminalEmulator::ScrollbackLine::~ScrollbackLine()
{
    clear();
}

TerminalEmulator::ScrollbackLine::ScrollbackLine(ScrollbackLine&& other) noexcept
    : cells(other.cells), cols(other.cols)
{
    other.cells = nullptr;
    other.cols = 0;
}

TerminalEmulator::ScrollbackLine& TerminalEmulator::ScrollbackLine::operator=(ScrollbackLine&& other) noexcept
{
    if (this != &other) {
        clear();
        cells = other.cells;
        cols = other.cols;
        other.cells = nullptr;
        other.cols = 0;
    }
    return *this;
}

bool TerminalEmulator::ScrollbackLine::assign(size_t cell_count)
{
    clear();
    if (cell_count == 0) {
        return true;
    }
    if (cell_count > 255) {
        cell_count = 255;
    }
    auto* next = static_cast<PackedTerminalCell*>(
        allocate_scrollback_bytes(cell_count * sizeof(PackedTerminalCell)));
    if (next == nullptr) {
        return false;
    }
    cells = next;
    cols = static_cast<uint8_t>(cell_count);
    return true;
}

void TerminalEmulator::ScrollbackLine::clear()
{
    free_scrollback_bytes(cells);
    cells = nullptr;
    cols = 0;
}

TerminalEmulator::TerminalEmulator(int cols, int rows, size_t scrollback_lines)
    : cols_(clamp_int(cols, 8, 80)),
      rows_(clamp_int(rows, 4, 24)),
      max_scrollback_lines_(scrollback_lines),
      dirty_(rows_, true)
{
    init_vterm();
}

TerminalEmulator::~TerminalEmulator()
{
    release_vterm();
}

void TerminalEmulator::reset()
{
    clear_scrollback();
    pending_output_.clear();
    alt_active_ = false;
    cursor_visible_ = true;
    cursor_ = {};
    if (screen_ != nullptr) {
        vterm_screen_reset(screen_, 1);
    }
    update_cursor_from_state();
    mark_all_dirty();
}

void TerminalEmulator::resize(int cols, int rows)
{
    cols = clamp_int(cols, 8, 80);
    rows = clamp_int(rows, 4, 24);
    if (cols == cols_ && rows == rows_) {
        return;
    }

    cols_ = cols;
    rows_ = rows;
    dirty_.assign(rows_, true);
    if (vt_ != nullptr) {
        vterm_set_size(vt_, rows_, cols_);
    }
    update_cursor_from_state();
    mark_all_dirty();
}

void TerminalEmulator::process(const std::string& bytes)
{
    if (vt_ == nullptr || screen_ == nullptr || bytes.empty()) {
        return;
    }
    vterm_input_write(vt_, bytes.data(), bytes.size());
    vterm_screen_flush_damage(screen_);
    update_cursor_from_state();
}

std::string TerminalEmulator::take_pending_output()
{
    std::string out;
    out.swap(pending_output_);
    return out;
}

std::string TerminalEmulator::encode_character(char ch)
{
    if (ch < 32 || ch == 0x7F || vt_ == nullptr) {
        return std::string(1, ch);
    }
    size_t before = pending_output_.size();
    vterm_keyboard_unichar(vt_, static_cast<unsigned char>(ch), VTERM_MOD_NONE);
    return capture_generated_output(before);
}

std::string TerminalEmulator::encode_key(TerminalInputKey key)
{
    if (vt_ == nullptr) {
        return {};
    }
    size_t before = pending_output_.size();
    vterm_keyboard_key(vt_, to_vterm_key(key), VTERM_MOD_NONE);
    return capture_generated_output(before);
}

void TerminalEmulator::mark_all_dirty()
{
    std::fill(dirty_.begin(), dirty_.end(), true);
}

void TerminalEmulator::clear_dirty()
{
    std::fill(dirty_.begin(), dirty_.end(), false);
}

const TerminalCell& TerminalEmulator::cell(int row, int col) const
{
    scratch_cell_ = kBlank;
    if (screen_ == nullptr || row < 0 || row >= rows_ || col < 0 || col >= cols_) {
        return scratch_cell_;
    }
    VTermScreenCell cell = {};
    VTermPos pos{row, col};
    if (vterm_screen_get_cell(screen_, pos, &cell)) {
        scratch_cell_ = convert_cell(cell);
    }
    return scratch_cell_;
}

std::vector<TerminalCell> TerminalEmulator::line(int row) const
{
    std::vector<TerminalCell> out(cols_, kBlank);
    if (screen_ == nullptr || row < 0 || row >= rows_) {
        return out;
    }
    for (int col = 0; col < cols_; ++col) {
        VTermScreenCell cell = {};
        VTermPos pos{row, col};
        if (vterm_screen_get_cell(screen_, pos, &cell)) {
            out[col] = convert_cell(cell);
        }
    }
    return out;
}

std::vector<std::vector<TerminalCell>> TerminalEmulator::scrollback_snapshot(int offset) const
{
    offset = clamp_int(offset, 0, max_scrollback_offset());
    std::vector<std::vector<TerminalCell>> view(rows_, blank_line());
    int scrollback_rows = static_cast<int>(scrollback_count_);
    int total = scrollback_rows + rows_;
    int start = std::max(0, total - rows_ - offset);
    for (int row = 0; row < rows_ && start + row < total; ++row) {
        int source = start + row;
        if (source < scrollback_rows) {
            copy_scrollback_line(static_cast<size_t>(source), view[row]);
        } else {
            view[row] = line(source - scrollback_rows);
        }
    }
    return view;
}

std::vector<int> TerminalEmulator::dirty_rows() const
{
    std::vector<int> rows;
    for (int row = 0; row < rows_; ++row) {
        if (dirty_[row]) {
            rows.push_back(row);
        }
    }
    return rows;
}

int TerminalEmulator::cols() const
{
    return cols_;
}

int TerminalEmulator::rows() const
{
    return rows_;
}

int TerminalEmulator::cursor_row() const
{
    return cursor_.row;
}

int TerminalEmulator::cursor_col() const
{
    return cursor_.col;
}

bool TerminalEmulator::cursor_visible() const
{
    return cursor_visible_;
}

bool TerminalEmulator::application_cursor_mode() const
{
    return false;
}

int TerminalEmulator::scrollback_size() const
{
    return static_cast<int>(scrollback_count_);
}

int TerminalEmulator::max_scrollback_offset() const
{
    return static_cast<int>(scrollback_count_);
}

std::vector<TerminalCell> TerminalEmulator::blank_line() const
{
    return std::vector<TerminalCell>(cols_, kBlank);
}

void TerminalEmulator::init_vterm()
{
    struct VTermBuilder builder = {};
    builder.rows = rows_;
    builder.cols = cols_;
    builder.outbuffer_len = 1024;
    builder.tmpbuffer_len = 1024;
    vt_ = vterm_build(&builder);
    if (vt_ == nullptr) {
        return;
    }

    vterm_set_utf8(vt_, 1);
    vterm_output_set_callback(vt_, &TerminalEmulator::output_callback, this);
    state_ = vterm_obtain_state(vt_);
    screen_ = vterm_obtain_screen(vt_);
    if (screen_ == nullptr) {
        return;
    }

    static const VTermScreenCallbacks callbacks = {
        &TerminalEmulator::damage_callback,
        &TerminalEmulator::moverect_callback,
        &TerminalEmulator::movecursor_callback,
        &TerminalEmulator::settermprop_callback,
        &TerminalEmulator::bell_callback,
        &TerminalEmulator::resize_callback,
        &TerminalEmulator::sb_pushline_callback,
        &TerminalEmulator::sb_popline_callback,
        &TerminalEmulator::sb_clear_callback,
        &TerminalEmulator::sb_pushline4_callback,
    };
    vterm_screen_set_callbacks(screen_, &callbacks, this);
    vterm_screen_callbacks_has_pushline4(screen_);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_ROW);
    vterm_screen_reset(screen_, 1);
    update_cursor_from_state();
    mark_all_dirty();
}

void TerminalEmulator::release_vterm()
{
    if (vt_ != nullptr) {
        vterm_free(vt_);
    }
    vt_ = nullptr;
    state_ = nullptr;
    screen_ = nullptr;
}

void TerminalEmulator::append_scrollback_line(int cols, const VTermScreenCell* cells)
{
    if (max_scrollback_lines_ == 0 || cells == nullptr || alt_active_) {
        return;
    }
    int limit = std::min(cols, cols_);
    while (limit > 0 && is_default_blank(pack_cell(cells[limit - 1]))) {
        --limit;
    }

    size_t bytes = static_cast<size_t>(limit) * sizeof(PackedTerminalCell);
    while (scrollback_count_ >= max_scrollback_lines_) {
        drop_oldest_scrollback_line();
    }
    while (scrollback_count_ > 0 && !has_scrollback_heap_budget(bytes)) {
        drop_oldest_scrollback_line();
    }
    if (!has_scrollback_heap_budget(bytes) || !ensure_scrollback_slot()) {
        return;
    }

    size_t slot = (scrollback_start_ + scrollback_count_) % scrollback_.size();
    if (!scrollback_[slot].assign(static_cast<size_t>(limit))) {
        return;
    }
    for (int col = 0; col < limit; ++col) {
        scrollback_[slot].cells[col] = pack_cell(cells[col]);
    }
    ++scrollback_count_;
}

TerminalEmulator::PackedTerminalCell TerminalEmulator::pack_cell(const VTermScreenCell& source) const
{
    PackedTerminalCell cell{};
    if (source.chars[0] == static_cast<uint32_t>(-1)) {
        cell.width = 0;
        cell.flags = kCellContinuation;
        return cell;
    }

    cell.codepoint = source.chars[0] == 0 ? ' ' : source.chars[0];
    cell.width = std::max<char>(1, source.width);
    cell.fg = vterm_color_to_ansi(source.fg, true);
    cell.bg = vterm_color_to_ansi(source.bg, false);
    if (source.attrs.bold) {
        cell.flags |= kCellBold;
    }
    if (source.attrs.reverse) {
        cell.flags |= kCellInverse;
    }
    if (source.attrs.underline != VTERM_UNDERLINE_OFF) {
        cell.flags |= kCellUnderline;
    }
    if (source.attrs.conceal) {
        cell.flags |= kCellHidden;
    }
    if (source.attrs.strike) {
        cell.flags |= kCellStrikethrough;
    }
    return cell;
}

TerminalCell TerminalEmulator::unpack_cell(const PackedTerminalCell& source) const
{
    TerminalCell cell{};
    cell.codepoint = source.codepoint == 0 ? ' ' : source.codepoint;
    cell.ch = cell.codepoint < 128 ? static_cast<char>(cell.codepoint) : '?';
    cell.fg = source.fg;
    cell.bg = source.bg;
    cell.width = source.width;
    cell.bold = (source.flags & kCellBold) != 0;
    cell.inverse = (source.flags & kCellInverse) != 0;
    cell.underline = (source.flags & kCellUnderline) != 0;
    cell.dim = (source.flags & kCellDim) != 0;
    cell.hidden = (source.flags & kCellHidden) != 0;
    cell.strikethrough = (source.flags & kCellStrikethrough) != 0;
    cell.continuation = (source.flags & kCellContinuation) != 0;
    return cell;
}

bool TerminalEmulator::is_default_blank(const PackedTerminalCell& cell) const
{
    return cell.codepoint == ' ' && cell.fg == kBlank.fg && cell.bg == kBlank.bg && cell.width == kBlank.width &&
           cell.flags == 0;
}

TerminalCell TerminalEmulator::convert_cell(const VTermScreenCell& source) const
{
    return unpack_cell(pack_cell(source));
}

const TerminalEmulator::ScrollbackLine* TerminalEmulator::scrollback_line(size_t logical_index) const
{
    if (logical_index >= scrollback_count_ || scrollback_.empty()) {
        return nullptr;
    }
    size_t slot = (scrollback_start_ + logical_index) % scrollback_.size();
    return &scrollback_[slot];
}

bool TerminalEmulator::ensure_scrollback_slot()
{
    if (scrollback_count_ < scrollback_.size()) {
        return true;
    }
    if (scrollback_.size() >= max_scrollback_lines_) {
        return false;
    }
    if (!has_scrollback_heap_budget(sizeof(ScrollbackLine))) {
        return false;
    }
    if (scrollback_start_ != 0 && !scrollback_.empty()) {
        std::rotate(scrollback_.begin(), scrollback_.begin() + scrollback_start_, scrollback_.end());
        scrollback_start_ = 0;
    }
    scrollback_.emplace_back();
    return true;
}

void TerminalEmulator::drop_oldest_scrollback_line()
{
    if (scrollback_count_ == 0 || scrollback_.empty()) {
        scrollback_start_ = 0;
        scrollback_count_ = 0;
        return;
    }
    scrollback_[scrollback_start_].clear();
    scrollback_start_ = (scrollback_start_ + 1) % scrollback_.size();
    --scrollback_count_;
    if (scrollback_count_ == 0) {
        scrollback_start_ = 0;
    }
}

void TerminalEmulator::clear_scrollback()
{
    for (auto& line : scrollback_) {
        line.clear();
    }
    std::vector<ScrollbackLine>().swap(scrollback_);
    scrollback_start_ = 0;
    scrollback_count_ = 0;
}

void TerminalEmulator::copy_scrollback_line(size_t logical_index, std::vector<TerminalCell>& out) const
{
    out.assign(cols_, kBlank);
    const auto* source = scrollback_line(logical_index);
    if (source == nullptr || source->cells == nullptr) {
        return;
    }
    int limit = std::min<int>(source->cols, cols_);
    for (int col = 0; col < limit; ++col) {
        out[col] = unpack_cell(source->cells[col]);
    }
}

void TerminalEmulator::mark_dirty(int row)
{
    if (row >= 0 && row < rows_) {
        dirty_[row] = true;
    }
}

void TerminalEmulator::mark_dirty_rect(VTermRect rect)
{
    int first = clamp_int(rect.start_row, 0, rows_ - 1);
    int last = clamp_int(rect.end_row - 1, 0, rows_ - 1);
    for (int row = first; row <= last; ++row) {
        mark_dirty(row);
    }
}

void TerminalEmulator::update_cursor(VTermPos pos, bool visible)
{
    int old_row = cursor_.row;
    cursor_.row = clamp_int(pos.row, 0, rows_ - 1);
    cursor_.col = clamp_int(pos.col, 0, cols_ - 1);
    cursor_visible_ = visible;
    mark_dirty(old_row);
    mark_dirty(cursor_.row);
}

void TerminalEmulator::update_cursor_from_state()
{
    if (state_ == nullptr) {
        return;
    }
    VTermPos pos{};
    vterm_state_get_cursorpos(state_, &pos);
    update_cursor(pos, cursor_visible_);
}

std::string TerminalEmulator::capture_generated_output(size_t before)
{
    if (before > pending_output_.size()) {
        return {};
    }
    std::string out = pending_output_.substr(before);
    pending_output_.resize(before);
    return out;
}

void TerminalEmulator::output_callback(const char* bytes, size_t len, void* user)
{
    auto* terminal = static_cast<TerminalEmulator*>(user);
    if (terminal != nullptr && bytes != nullptr && len > 0) {
        terminal->pending_output_.append(bytes, bytes + len);
    }
}

int TerminalEmulator::damage_callback(VTermRect rect, void* user)
{
    static_cast<TerminalEmulator*>(user)->mark_dirty_rect(rect);
    return 1;
}

int TerminalEmulator::moverect_callback(VTermRect dest, VTermRect src, void* user)
{
    auto* terminal = static_cast<TerminalEmulator*>(user);
    terminal->mark_dirty_rect(src);
    terminal->mark_dirty_rect(dest);
    return 1;
}

int TerminalEmulator::movecursor_callback(VTermPos pos, VTermPos oldpos, int visible, void* user)
{
    auto* terminal = static_cast<TerminalEmulator*>(user);
    terminal->mark_dirty(oldpos.row);
    terminal->update_cursor(pos, visible != 0);
    return 1;
}

int TerminalEmulator::settermprop_callback(VTermProp prop, VTermValue* value, void* user)
{
    auto* terminal = static_cast<TerminalEmulator*>(user);
    if (terminal == nullptr || value == nullptr) {
        return 0;
    }
    switch (prop) {
        case VTERM_PROP_CURSORVISIBLE:
            terminal->cursor_visible_ = value->boolean != 0;
            terminal->mark_dirty(terminal->cursor_.row);
            return 1;
        case VTERM_PROP_ALTSCREEN:
            terminal->alt_active_ = value->boolean != 0;
            terminal->mark_all_dirty();
            return 1;
        default:
            return 1;
    }
}

int TerminalEmulator::bell_callback(void* user)
{
    return 1;
}

int TerminalEmulator::resize_callback(int rows, int cols, void* user)
{
    auto* terminal = static_cast<TerminalEmulator*>(user);
    if (terminal == nullptr) {
        return 0;
    }
    terminal->rows_ = clamp_int(rows, 4, 24);
    terminal->cols_ = clamp_int(cols, 8, 80);
    terminal->dirty_.assign(terminal->rows_, true);
    return 1;
}

int TerminalEmulator::sb_pushline_callback(int cols, const VTermScreenCell* cells, void* user)
{
    static_cast<TerminalEmulator*>(user)->append_scrollback_line(cols, cells);
    return 1;
}

int TerminalEmulator::sb_popline_callback(int cols, VTermScreenCell* cells, void* user)
{
    return 0;
}

int TerminalEmulator::sb_clear_callback(void* user)
{
    auto* terminal = static_cast<TerminalEmulator*>(user);
    if (terminal != nullptr) {
        terminal->clear_scrollback();
    }
    return 1;
}

int TerminalEmulator::sb_pushline4_callback(int cols, const VTermScreenCell* cells, bool continuation, void* user)
{
    static_cast<TerminalEmulator*>(user)->append_scrollback_line(cols, cells);
    return 1;
}

}  // namespace adv
