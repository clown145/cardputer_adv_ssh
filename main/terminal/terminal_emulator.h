#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace adv {

struct TerminalCell {
    char ch = ' ';
    uint8_t fg = 7;
    uint8_t bg = 0;
    bool bold = false;
    bool inverse = false;
};

class TerminalEmulator {
public:
    static constexpr int kCols = 40;
    static constexpr int kRows = 9;

    TerminalEmulator();

    void reset();
    void process(const std::string& bytes);
    void mark_all_dirty();
    void clear_dirty();

    const TerminalCell& cell(int row, int col) const;
    std::vector<int> dirty_rows() const;
    int cursor_row() const;
    int cursor_col() const;
    bool cursor_visible() const;

private:
    enum class ParserState {
        kGround,
        kEsc,
        kCsi,
        kOsc,
        kOscEsc,
    };

    struct CursorState {
        int row = 0;
        int col = 0;
    };

    std::vector<TerminalCell>& active_cells();
    const std::vector<TerminalCell>& active_cells() const;
    TerminalCell& active_cell(int row, int col);
    void mark_dirty(int row);
    void put_char(char ch);
    void line_feed();
    void reverse_index();
    void carriage_return();
    void backspace();
    void tab();
    void scroll_up(int top, int bottom, int count);
    void scroll_down(int top, int bottom, int count);
    void clear_cells(int row_start, int col_start, int row_end, int col_end);
    void clear_line(int mode);
    void clear_screen(int mode);
    void set_cursor(int row, int col);
    void move_cursor(int row_delta, int col_delta);
    void save_cursor();
    void restore_cursor();
    void reset_style();
    void apply_sgr();
    void enter_alt_screen(bool save);
    void leave_alt_screen(bool restore);
    void clear_active_screen();
    void handle_escape(uint8_t ch);
    void begin_csi();
    void collect_csi(uint8_t ch);
    void dispatch_csi(uint8_t final);
    int param(size_t index, int fallback) const;
    bool private_param(int value) const;

    std::vector<TerminalCell> main_cells_;
    std::vector<TerminalCell> alt_cells_;
    std::vector<bool> dirty_;
    bool alt_active_ = false;
    ParserState state_ = ParserState::kGround;
    CursorState cursor_;
    CursorState saved_cursor_;
    CursorState saved_main_cursor_;
    int scroll_top_ = 0;
    int scroll_bottom_ = kRows - 1;
    bool cursor_visible_ = true;
    TerminalCell style_;
    std::vector<int> csi_params_;
    int csi_current_ = -1;
    char csi_private_ = '\0';
};

}  // namespace adv
