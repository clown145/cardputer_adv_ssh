#include "terminal/terminal_emulator.h"

#include <algorithm>

namespace adv {
namespace {
constexpr TerminalCell kBlank{};

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

bool is_combining(uint32_t codepoint)
{
    return (codepoint >= 0x0300 && codepoint <= 0x036F) || (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) ||
           (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) || (codepoint >= 0x20D0 && codepoint <= 0x20FF) ||
           (codepoint >= 0xFE20 && codepoint <= 0xFE2F);
}

int codepoint_width(uint32_t codepoint)
{
    if (is_combining(codepoint)) {
        return 0;
    }
    if ((codepoint >= 0x1100 && codepoint <= 0x115F) || (codepoint >= 0x2E80 && codepoint <= 0xA4CF) ||
        (codepoint >= 0xAC00 && codepoint <= 0xD7A3) || (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
        (codepoint >= 0x20000 && codepoint <= 0x3FFFD) ||
        (codepoint >= 0xFE10 && codepoint <= 0xFE19) || (codepoint >= 0xFE30 && codepoint <= 0xFE6F) ||
        (codepoint >= 0xFF00 && codepoint <= 0xFF60) || (codepoint >= 0xFFE0 && codepoint <= 0xFFE6)) {
        return 2;
    }
    return 1;
}

char fallback_for_codepoint(uint32_t codepoint)
{
    switch (codepoint) {
        case 0x2500:
        case 0x2501:
        case 0x2550:
            return '-';
        case 0x2502:
        case 0x2503:
        case 0x2551:
            return '|';
        case 0x250C:
        case 0x2510:
        case 0x2514:
        case 0x2518:
        case 0x251C:
        case 0x2524:
        case 0x252C:
        case 0x2534:
        case 0x253C:
        case 0x2554:
        case 0x2557:
        case 0x255A:
        case 0x255D:
        case 0x256C:
            return '+';
        case 0x2588:
        case 0x2593:
        case 0x25A0:
            return '#';
        case 0x2591:
        case 0x2592:
            return '.';
        case 0x2190:
            return '<';
        case 0x2191:
            return '^';
        case 0x2192:
            return '>';
        case 0x2193:
            return 'v';
        case 0x2022:
        case 0x00B7:
            return '*';
        default:
            return codepoint < 128 ? static_cast<char>(codepoint) : '?';
    }
}

uint32_t drawable_codepoint(uint32_t codepoint, int width)
{
    if (codepoint < 128) {
        return codepoint;
    }
    if (width == 2 && codepoint <= 0xFFFF) {
        return codepoint;
    }
    return static_cast<uint8_t>(fallback_for_codepoint(codepoint));
}
}  // namespace

TerminalEmulator::TerminalEmulator()
    : main_cells_(kRows * kCols), alt_cells_(kRows * kCols), dirty_(kRows, true)
{
    reset_style();
}

void TerminalEmulator::reset()
{
    std::fill(main_cells_.begin(), main_cells_.end(), kBlank);
    std::fill(alt_cells_.begin(), alt_cells_.end(), kBlank);
    alt_active_ = false;
    state_ = ParserState::kGround;
    cursor_ = {};
    saved_cursor_ = {};
    saved_main_cursor_ = {};
    scroll_top_ = 0;
    scroll_bottom_ = kRows - 1;
    cursor_visible_ = true;
    auto_wrap_ = true;
    wrap_pending_ = false;
    origin_mode_ = false;
    insert_mode_ = false;
    selecting_charset_ = false;
    dec_graphics_ = false;
    saved_dec_graphics_ = false;
    application_cursor_mode_ = false;
    last_printed_codepoint_ = ' ';
    last_printed_width_ = 1;
    pending_output_.clear();
    utf8_codepoint_ = 0;
    utf8_remaining_ = 0;
    utf8_expected_ = 0;
    reset_style();
    mark_all_dirty();
}

void TerminalEmulator::process(const std::string& bytes)
{
    for (uint8_t ch : bytes) {
        switch (state_) {
            case ParserState::kGround:
                if (ch == 0x1B) {
                    state_ = ParserState::kEsc;
                } else if (ch == 0x0E || ch == 0x0F) {
                    // Shift-out/shift-in selects alternate character sets on VT terminals.
                    // We render ASCII only, so the state change is intentionally ignored.
                } else if (ch == '\r') {
                    carriage_return();
                } else if (ch == '\n') {
                    line_feed();
                } else if (ch == '\b' || ch == 0x7F) {
                    backspace();
                } else if (ch == '\t') {
                    tab();
                } else if (ch >= 32 && ch <= 126) {
                    put_char(static_cast<char>(ch));
                } else if (ch >= 0x80) {
                    handle_utf8_byte(ch);
                }
                break;
            case ParserState::kEsc:
                handle_escape(ch);
                break;
            case ParserState::kCsi:
                collect_csi(ch);
                break;
            case ParserState::kOsc:
                if (ch == 0x07) {
                    state_ = ParserState::kGround;
                } else if (ch == 0x1B) {
                    state_ = ParserState::kOscEsc;
                }
                break;
            case ParserState::kOscEsc:
                state_ = ch == '\\' ? ParserState::kGround : ParserState::kOsc;
                break;
            case ParserState::kIgnoreOne:
                state_ = ParserState::kGround;
                break;
        }
    }
}

void TerminalEmulator::mark_all_dirty()
{
    std::fill(dirty_.begin(), dirty_.end(), true);
}

void TerminalEmulator::clear_dirty()
{
    std::fill(dirty_.begin(), dirty_.end(), false);
}

std::string TerminalEmulator::take_pending_output()
{
    std::string out;
    out.swap(pending_output_);
    return out;
}

const TerminalCell& TerminalEmulator::cell(int row, int col) const
{
    return active_cells()[row * kCols + col];
}

std::vector<int> TerminalEmulator::dirty_rows() const
{
    std::vector<int> rows;
    for (int row = 0; row < kRows; ++row) {
        if (dirty_[row]) {
            rows.push_back(row);
        }
    }
    return rows;
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
    return application_cursor_mode_;
}

std::vector<TerminalCell>& TerminalEmulator::active_cells()
{
    return alt_active_ ? alt_cells_ : main_cells_;
}

const std::vector<TerminalCell>& TerminalEmulator::active_cells() const
{
    return alt_active_ ? alt_cells_ : main_cells_;
}

TerminalCell& TerminalEmulator::active_cell(int row, int col)
{
    return active_cells()[row * kCols + col];
}

void TerminalEmulator::mark_dirty(int row)
{
    if (row >= 0 && row < kRows) {
        dirty_[row] = true;
    }
}

void TerminalEmulator::put_char(char ch)
{
    if (dec_graphics_) {
        switch (ch) {
            case 'j':
            case 'k':
            case 'l':
            case 'm':
            case 'n':
            case 't':
            case 'u':
            case 'v':
            case 'w':
                ch = '+';
                break;
            case 'q':
                ch = '-';
                break;
            case 'x':
                ch = '|';
                break;
            case '`':
            case 'a':
                ch = '.';
                break;
            default:
                break;
        }
    }
    put_glyph(ch, 1);
}

void TerminalEmulator::put_codepoint(uint32_t codepoint)
{
    int width = codepoint_width(codepoint);
    if (width == 0) {
        return;
    }
    put_glyph(drawable_codepoint(codepoint, width), width);
}

void TerminalEmulator::put_glyph(uint32_t codepoint, int width)
{
    width = clamp_int(width, 1, 2);
    if (wrap_pending_) {
        cursor_.col = 0;
        line_feed();
        wrap_pending_ = false;
    }
    if (width == 2 && cursor_.col == kCols - 1) {
        cursor_.col = 0;
        line_feed();
    }
    if (insert_mode_) {
        for (int col = kCols - 1; col >= cursor_.col + width; --col) {
            active_cell(cursor_.row, col) = active_cell(cursor_.row, col - width);
        }
    }
    for (int col = cursor_.col; col < cursor_.col + width && col < kCols; ++col) {
        clear_wide_fragment(cursor_.row, col);
    }
    active_cell(cursor_.row, cursor_.col) = style_;
    active_cell(cursor_.row, cursor_.col).codepoint = codepoint;
    active_cell(cursor_.row, cursor_.col).ch = codepoint < 128 ? static_cast<char>(codepoint) : '?';
    active_cell(cursor_.row, cursor_.col).width = width;
    active_cell(cursor_.row, cursor_.col).continuation = false;
    last_printed_codepoint_ = codepoint;
    last_printed_width_ = width;
    if (width == 2 && cursor_.col + 1 < kCols) {
        active_cell(cursor_.row, cursor_.col + 1) = style_;
        active_cell(cursor_.row, cursor_.col + 1).codepoint = ' ';
        active_cell(cursor_.row, cursor_.col + 1).ch = ' ';
        active_cell(cursor_.row, cursor_.col + 1).width = 0;
        active_cell(cursor_.row, cursor_.col + 1).continuation = true;
    }
    mark_dirty(cursor_.row);
    if (cursor_.col + width >= kCols) {
        cursor_.col = kCols - 1;
        wrap_pending_ = auto_wrap_;
    } else {
        cursor_.col += width;
    }
}

void TerminalEmulator::clear_wide_fragment(int row, int col)
{
    if (row < 0 || row >= kRows || col < 0 || col >= kCols) {
        return;
    }
    TerminalCell& cell = active_cell(row, col);
    if (cell.continuation && col > 0) {
        active_cell(row, col - 1) = kBlank;
    } else if (!cell.continuation && cell.width == 2 && col + 1 < kCols) {
        active_cell(row, col + 1) = kBlank;
    }
    cell = kBlank;
}

void TerminalEmulator::handle_utf8_byte(uint8_t ch)
{
    if (utf8_remaining_ == 0) {
        if ((ch & 0xE0) == 0xC0) {
            utf8_codepoint_ = ch & 0x1F;
            utf8_remaining_ = 1;
            utf8_expected_ = 1;
        } else if ((ch & 0xF0) == 0xE0) {
            utf8_codepoint_ = ch & 0x0F;
            utf8_remaining_ = 2;
            utf8_expected_ = 2;
        } else if ((ch & 0xF8) == 0xF0) {
            utf8_codepoint_ = ch & 0x07;
            utf8_remaining_ = 3;
            utf8_expected_ = 3;
        } else {
            put_glyph('?', 1);
        }
        return;
    }

    if ((ch & 0xC0) != 0x80) {
        utf8_remaining_ = 0;
        utf8_expected_ = 0;
        put_glyph('?', 1);
        handle_utf8_byte(ch);
        return;
    }
    utf8_codepoint_ = (utf8_codepoint_ << 6) | (ch & 0x3F);
    --utf8_remaining_;
    if (utf8_remaining_ == 0) {
        uint32_t min_value = utf8_expected_ == 1 ? 0x80 : (utf8_expected_ == 2 ? 0x800 : 0x10000);
        if (utf8_codepoint_ < min_value || utf8_codepoint_ > 0x10FFFF ||
            (utf8_codepoint_ >= 0xD800 && utf8_codepoint_ <= 0xDFFF)) {
            put_glyph('?', 1);
        } else {
            put_codepoint(utf8_codepoint_);
        }
        utf8_codepoint_ = 0;
        utf8_expected_ = 0;
    }
}

void TerminalEmulator::line_feed()
{
    if (cursor_.row == scroll_bottom_) {
        scroll_up(scroll_top_, scroll_bottom_, 1);
    } else if (cursor_.row < kRows - 1) {
        ++cursor_.row;
    }
}

void TerminalEmulator::reverse_index()
{
    if (cursor_.row == scroll_top_) {
        scroll_down(scroll_top_, scroll_bottom_, 1);
    } else if (cursor_.row > 0) {
        --cursor_.row;
    }
}

void TerminalEmulator::carriage_return()
{
    cursor_.col = 0;
    wrap_pending_ = false;
}

void TerminalEmulator::backspace()
{
    wrap_pending_ = false;
    if (cursor_.col > 0) {
        --cursor_.col;
    }
}

void TerminalEmulator::tab()
{
    wrap_pending_ = false;
    int next = ((cursor_.col / 4) + 1) * 4;
    cursor_.col = clamp_int(next, 0, kCols - 1);
}

void TerminalEmulator::scroll_up(int top, int bottom, int count)
{
    top = clamp_int(top, 0, kRows - 1);
    bottom = clamp_int(bottom, top, kRows - 1);
    count = clamp_int(count, 1, bottom - top + 1);
    auto& cells = active_cells();
    for (int row = top; row <= bottom - count; ++row) {
        for (int col = 0; col < kCols; ++col) {
            cells[row * kCols + col] = cells[(row + count) * kCols + col];
        }
        mark_dirty(row);
    }
    for (int row = bottom - count + 1; row <= bottom; ++row) {
        for (int col = 0; col < kCols; ++col) {
            cells[row * kCols + col] = kBlank;
        }
        mark_dirty(row);
    }
}

void TerminalEmulator::scroll_down(int top, int bottom, int count)
{
    top = clamp_int(top, 0, kRows - 1);
    bottom = clamp_int(bottom, top, kRows - 1);
    count = clamp_int(count, 1, bottom - top + 1);
    auto& cells = active_cells();
    for (int row = bottom; row >= top + count; --row) {
        for (int col = 0; col < kCols; ++col) {
            cells[row * kCols + col] = cells[(row - count) * kCols + col];
        }
        mark_dirty(row);
    }
    for (int row = top; row < top + count; ++row) {
        for (int col = 0; col < kCols; ++col) {
            cells[row * kCols + col] = kBlank;
        }
        mark_dirty(row);
    }
}

void TerminalEmulator::clear_cells(int row_start, int col_start, int row_end, int col_end)
{
    row_start = clamp_int(row_start, 0, kRows - 1);
    row_end = clamp_int(row_end, 0, kRows - 1);
    col_start = clamp_int(col_start, 0, kCols - 1);
    col_end = clamp_int(col_end, 0, kCols - 1);
    if (row_start > row_end) {
        std::swap(row_start, row_end);
    }
    for (int row = row_start; row <= row_end; ++row) {
        int first = row == row_start ? col_start : 0;
        int last = row == row_end ? col_end : kCols - 1;
        for (int col = first; col <= last; ++col) {
            active_cell(row, col) = kBlank;
        }
        mark_dirty(row);
    }
}

void TerminalEmulator::clear_line(int mode)
{
    if (mode == 1) {
        clear_cells(cursor_.row, 0, cursor_.row, cursor_.col);
    } else if (mode == 2) {
        clear_cells(cursor_.row, 0, cursor_.row, kCols - 1);
    } else {
        clear_cells(cursor_.row, cursor_.col, cursor_.row, kCols - 1);
    }
}

void TerminalEmulator::clear_screen(int mode)
{
    if (mode == 1) {
        clear_cells(0, 0, cursor_.row, cursor_.col);
    } else if (mode == 2 || mode == 3) {
        clear_cells(0, 0, kRows - 1, kCols - 1);
    } else {
        clear_cells(cursor_.row, cursor_.col, kRows - 1, kCols - 1);
    }
}

void TerminalEmulator::set_cursor(int row, int col)
{
    wrap_pending_ = false;
    cursor_.row = clamp_int(row, 0, kRows - 1);
    cursor_.col = clamp_int(col, 0, kCols - 1);
}

void TerminalEmulator::move_cursor(int row_delta, int col_delta)
{
    wrap_pending_ = false;
    set_cursor(cursor_.row + row_delta, cursor_.col + col_delta);
}

void TerminalEmulator::set_scroll_region(int top, int bottom)
{
    if (top >= 0 && bottom > top && bottom < kRows) {
        scroll_top_ = top;
        scroll_bottom_ = bottom;
    } else {
        scroll_top_ = 0;
        scroll_bottom_ = kRows - 1;
    }
    set_cursor(0, 0);
}

void TerminalEmulator::save_cursor()
{
    saved_cursor_ = cursor_;
    saved_dec_graphics_ = dec_graphics_;
}

void TerminalEmulator::restore_cursor()
{
    set_cursor(saved_cursor_.row, saved_cursor_.col);
    dec_graphics_ = saved_dec_graphics_;
}

void TerminalEmulator::reset_style()
{
    style_ = kBlank;
}

void TerminalEmulator::apply_sgr()
{
    if (csi_params_.empty()) {
        reset_style();
        return;
    }
    for (size_t i = 0; i < csi_params_.size(); ++i) {
        int value = csi_params_[i] < 0 ? 0 : csi_params_[i];
        if (value == 0) {
            reset_style();
        } else if (value == 2) {
            style_.bold = false;
        } else if (value == 1) {
            style_.bold = true;
        } else if (value == 4) {
            style_.underline = true;
        } else if (value == 22) {
            style_.bold = false;
        } else if (value == 24) {
            style_.underline = false;
        } else if (value == 7) {
            style_.inverse = true;
        } else if (value == 27) {
            style_.inverse = false;
        } else if (value >= 30 && value <= 37) {
            style_.fg = value - 30;
        } else if (value >= 40 && value <= 47) {
            style_.bg = value - 40;
        } else if (value == 38) {
            apply_extended_color(true, i);
        } else if (value == 48) {
            apply_extended_color(false, i);
        } else if (value >= 90 && value <= 97) {
            style_.fg = value - 90 + 8;
        } else if (value >= 100 && value <= 107) {
            style_.bg = value - 100 + 8;
        } else if (value == 39) {
            style_.fg = 7;
        } else if (value == 49) {
            style_.bg = 0;
        }
    }
}

void TerminalEmulator::apply_extended_color(bool foreground, size_t& index)
{
    if (index + 1 >= csi_params_.size()) {
        return;
    }
    int mode = csi_params_[index + 1];
    uint8_t color = foreground ? style_.fg : style_.bg;
    if (mode == 5 && index + 2 < csi_params_.size()) {
        int value = csi_params_[index + 2];
        if (value >= 0 && value <= 255) {
            color = ansi256_to_ansi(static_cast<uint8_t>(value));
        }
        index += 2;
    } else if (mode == 2 && index + 4 < csi_params_.size()) {
        int r = clamp_int(csi_params_[index + 2], 0, 255);
        int g = clamp_int(csi_params_[index + 3], 0, 255);
        int b = clamp_int(csi_params_[index + 4], 0, 255);
        color = rgb_to_ansi(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
        index += 4;
    }
    if (foreground) {
        style_.fg = color;
    } else {
        style_.bg = color;
    }
}

void TerminalEmulator::apply_mode(bool enabled)
{
    if (csi_private_ == '?') {
        if (private_param(1)) {
            application_cursor_mode_ = enabled;
        }
        if (private_param(25)) {
            cursor_visible_ = enabled;
            mark_dirty(cursor_.row);
        }
        if (private_param(6)) {
            origin_mode_ = enabled;
            set_cursor(0, 0);
        }
        if (private_param(7)) {
            auto_wrap_ = enabled;
            if (!enabled) {
                wrap_pending_ = false;
            }
        }
        if (enabled) {
            if (private_param(47) || private_param(1047)) {
                enter_alt_screen(false);
            }
            if (private_param(1049)) {
                enter_alt_screen(true);
            }
        } else {
            if (private_param(47) || private_param(1047)) {
                leave_alt_screen(false);
            }
            if (private_param(1049)) {
                leave_alt_screen(true);
            }
        }
        return;
    }

    for (int value : csi_params_) {
        if (value == 4) {
            insert_mode_ = enabled;
        }
    }
}

void TerminalEmulator::enter_alt_screen(bool save)
{
    if (save) {
        saved_main_cursor_ = cursor_;
    }
    alt_active_ = true;
    clear_active_screen();
    cursor_ = {};
    scroll_top_ = 0;
    scroll_bottom_ = kRows - 1;
    mark_all_dirty();
}

void TerminalEmulator::leave_alt_screen(bool restore)
{
    alt_active_ = false;
    if (restore) {
        cursor_ = saved_main_cursor_;
    }
    scroll_top_ = 0;
    scroll_bottom_ = kRows - 1;
    mark_all_dirty();
}

void TerminalEmulator::clear_active_screen()
{
    std::fill(active_cells().begin(), active_cells().end(), kBlank);
    mark_all_dirty();
}

void TerminalEmulator::append_device_attributes()
{
    pending_output_ += "\x1B[?62;1;2;6;8;9;15;c";
}

void TerminalEmulator::append_secondary_device_attributes()
{
    pending_output_ += "\x1B[>0;115;0c";
}

void TerminalEmulator::append_cursor_report(bool dec)
{
    pending_output_ += "\x1B[";
    if (dec) {
        pending_output_ += "?";
    }
    pending_output_ += std::to_string(cursor_.row + 1);
    pending_output_ += ";";
    pending_output_ += std::to_string(cursor_.col + 1);
    pending_output_ += "R";
}

void TerminalEmulator::handle_escape(uint8_t ch)
{
    if (selecting_charset_) {
        handle_charset_select(ch);
        return;
    }
    if (ch == '[') {
        begin_csi();
        return;
    }
    if (ch == '(' || ch == ')' || ch == '*' || ch == '+' || ch == '-' || ch == '.' || ch == '/' || ch == '%') {
        selecting_charset_ = true;
        return;
    }
    if (ch == '#') {
        state_ = ParserState::kIgnoreOne;
        return;
    }
    if (ch == ']' || ch == 'P' || ch == '^' || ch == '_') {
        state_ = ParserState::kOsc;
        return;
    }
    if (ch == '7') {
        save_cursor();
    } else if (ch == '8') {
        restore_cursor();
    } else if (ch == 'D') {
        line_feed();
    } else if (ch == 'M') {
        reverse_index();
    } else if (ch == 'E') {
        line_feed();
        carriage_return();
    } else if (ch == '=') {
        // Application keypad mode. The Cardputer keyboard sends explicit escape
        // sequences, so keypad mode does not change rendering state.
    } else if (ch == '>') {
        // Numeric keypad mode.
    } else if (ch == 'c') {
        reset();
    } else if (ch == 'Z') {
        append_device_attributes();
    }
    state_ = ParserState::kGround;
}

void TerminalEmulator::handle_charset_select(uint8_t ch)
{
    dec_graphics_ = ch == '0';
    selecting_charset_ = false;
    state_ = ParserState::kGround;
}

void TerminalEmulator::begin_csi()
{
    csi_params_.clear();
    csi_current_ = -1;
    csi_private_ = '\0';
    state_ = ParserState::kCsi;
}

void TerminalEmulator::collect_csi(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') {
        if (csi_current_ < 0) {
            csi_current_ = 0;
        }
        csi_current_ = csi_current_ * 10 + (ch - '0');
        return;
    }
    if (ch == ';' || ch == ':') {
        csi_params_.push_back(csi_current_);
        csi_current_ = -1;
        return;
    }
    if (ch >= 0x20 && ch <= 0x2F) {
        return;
    }
    if ((ch == '?' || ch == '>' || ch == '=') && csi_params_.empty() && csi_current_ < 0) {
        csi_private_ = static_cast<char>(ch);
        return;
    }
    if (ch >= 0x40 && ch <= 0x7E) {
        csi_params_.push_back(csi_current_);
        dispatch_csi(ch);
        state_ = ParserState::kGround;
    }
}

void TerminalEmulator::dispatch_csi(uint8_t final)
{
    int count = param(0, 1);
    switch (final) {
        case 'A':
            move_cursor(-count, 0);
            break;
        case 'B':
            move_cursor(count, 0);
            break;
        case 'C':
            move_cursor(0, count);
            break;
        case 'D':
            move_cursor(0, -count);
            break;
        case 'E':
            move_cursor(count, 0);
            carriage_return();
            break;
        case 'F':
            move_cursor(-count, 0);
            carriage_return();
            break;
        case 'G':
            set_cursor(cursor_.row, param(0, 1) - 1);
            break;
        case 'H':
        case 'f': {
            int row = param(0, 1) - 1;
            if (origin_mode_) {
                row += scroll_top_;
                row = clamp_int(row, scroll_top_, scroll_bottom_);
            }
            set_cursor(row, param(1, 1) - 1);
            break;
        }
        case 'J':
            clear_screen(param(0, 0));
            break;
        case 'K':
            clear_line(param(0, 0));
            break;
        case 'L':
            scroll_down(cursor_.row, scroll_bottom_, count);
            break;
        case 'M':
            scroll_up(cursor_.row, scroll_bottom_, count);
            break;
        case 'P':
            for (int col = cursor_.col; col < kCols; ++col) {
                int src = col + count;
                active_cell(cursor_.row, col) = src < kCols ? active_cell(cursor_.row, src) : kBlank;
            }
            mark_dirty(cursor_.row);
            break;
        case 'Z':
            cursor_.col = clamp_int(cursor_.col - count * 4, 0, kCols - 1);
            break;
        case '@':
            for (int col = kCols - 1; col >= cursor_.col; --col) {
                int src = col - count;
                active_cell(cursor_.row, col) = src >= cursor_.col ? active_cell(cursor_.row, src) : kBlank;
            }
            mark_dirty(cursor_.row);
            break;
        case 'X':
            clear_cells(cursor_.row, cursor_.col, cursor_.row, cursor_.col + count - 1);
            break;
        case 'a':
            move_cursor(0, count);
            break;
        case 'b':
            for (int i = 0; i < count; ++i) {
                put_glyph(last_printed_codepoint_, last_printed_width_);
            }
            break;
        case 'c':
            if (csi_private_ == '>') {
                append_secondary_device_attributes();
            } else {
                append_device_attributes();
            }
            break;
        case 'S':
            scroll_up(scroll_top_, scroll_bottom_, count);
            break;
        case 'T':
            scroll_down(scroll_top_, scroll_bottom_, count);
            break;
        case 'd':
            set_cursor((origin_mode_ ? scroll_top_ : 0) + param(0, 1) - 1, cursor_.col);
            break;
        case 'e':
            move_cursor(count, 0);
            break;
        case 'g':
            break;
        case 'm':
            apply_sgr();
            break;
        case 'n':
            if (csi_private_ == '?' && param(0, 0) == 6) {
                append_cursor_report(true);
            } else if (param(0, 0) == 5) {
                pending_output_ += "\x1B[0n";
            } else if (param(0, 0) == 6) {
                append_cursor_report(false);
            }
            break;
        case 'r': {
            int top = param(0, 1) - 1;
            int bottom = param(1, kRows) - 1;
            set_scroll_region(top, bottom);
            break;
        }
        case 's':
            save_cursor();
            break;
        case 'u':
            restore_cursor();
            break;
        case 'h':
            apply_mode(true);
            break;
        case 'l':
            apply_mode(false);
            break;
        default:
            break;
    }
}

int TerminalEmulator::param(size_t index, int fallback) const
{
    if (index >= csi_params_.size() || csi_params_[index] <= 0) {
        return fallback;
    }
    return csi_params_[index];
}

bool TerminalEmulator::private_param(int value) const
{
    if (csi_private_ != '?') {
        return false;
    }
    return std::find(csi_params_.begin(), csi_params_.end(), value) != csi_params_.end();
}

}  // namespace adv
