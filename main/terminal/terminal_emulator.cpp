#include "terminal/terminal_emulator.h"

#include <algorithm>

namespace adv {
namespace {
constexpr TerminalCell kBlank{};

int clamp_int(int value, int low, int high)
{
    return std::max(low, std::min(value, high));
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
    if (cursor_.col >= kCols) {
        cursor_.col = 0;
        line_feed();
    }
    active_cell(cursor_.row, cursor_.col) = style_;
    active_cell(cursor_.row, cursor_.col).ch = ch;
    mark_dirty(cursor_.row);
    ++cursor_.col;
    if (cursor_.col >= kCols) {
        cursor_.col = 0;
        line_feed();
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
}

void TerminalEmulator::backspace()
{
    if (cursor_.col > 0) {
        --cursor_.col;
    }
}

void TerminalEmulator::tab()
{
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
    cursor_.row = clamp_int(row, 0, kRows - 1);
    cursor_.col = clamp_int(col, 0, kCols - 1);
}

void TerminalEmulator::move_cursor(int row_delta, int col_delta)
{
    set_cursor(cursor_.row + row_delta, cursor_.col + col_delta);
}

void TerminalEmulator::save_cursor()
{
    saved_cursor_ = cursor_;
}

void TerminalEmulator::restore_cursor()
{
    set_cursor(saved_cursor_.row, saved_cursor_.col);
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
        } else if (value == 1) {
            style_.bold = true;
        } else if (value == 22) {
            style_.bold = false;
        } else if (value == 7) {
            style_.inverse = true;
        } else if (value == 27) {
            style_.inverse = false;
        } else if (value >= 30 && value <= 37) {
            style_.fg = value - 30;
        } else if (value >= 40 && value <= 47) {
            style_.bg = value - 40;
        } else if (value >= 90 && value <= 97) {
            style_.fg = value - 90;
            style_.bold = true;
        } else if (value >= 100 && value <= 107) {
            style_.bg = value - 100;
        } else if (value == 39) {
            style_.fg = 7;
        } else if (value == 49) {
            style_.bg = 0;
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

void TerminalEmulator::handle_escape(uint8_t ch)
{
    if (ch == '[') {
        begin_csi();
        return;
    }
    if (ch == ']') {
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
    } else if (ch == 'c') {
        reset();
    }
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
        case 'f':
            set_cursor(param(0, 1) - 1, param(1, 1) - 1);
            break;
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
        case 'S':
            scroll_up(scroll_top_, scroll_bottom_, count);
            break;
        case 'T':
            scroll_down(scroll_top_, scroll_bottom_, count);
            break;
        case 'd':
            set_cursor(param(0, 1) - 1, cursor_.col);
            break;
        case 'm':
            apply_sgr();
            break;
        case 'r': {
            int top = param(0, 1) - 1;
            int bottom = param(1, kRows) - 1;
            if (top >= 0 && bottom > top && bottom < kRows) {
                scroll_top_ = top;
                scroll_bottom_ = bottom;
                set_cursor(0, 0);
            }
            break;
        }
        case 's':
            save_cursor();
            break;
        case 'u':
            restore_cursor();
            break;
        case 'h':
            if (private_param(25)) {
                cursor_visible_ = true;
                mark_dirty(cursor_.row);
            }
            if (private_param(47) || private_param(1047)) {
                enter_alt_screen(false);
            }
            if (private_param(1049)) {
                enter_alt_screen(true);
            }
            break;
        case 'l':
            if (private_param(25)) {
                cursor_visible_ = false;
                mark_dirty(cursor_.row);
            }
            if (private_param(47) || private_param(1047)) {
                leave_alt_screen(false);
            }
            if (private_param(1049)) {
                leave_alt_screen(true);
            }
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
