#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace adv {

struct TerminalCell {
    char ch = ' ';
    uint32_t codepoint = ' ';
    uint8_t fg = 7;
    uint8_t bg = 0;
    uint8_t width = 1;
    bool bold = false;
    bool inverse = false;
    bool underline = false;
    bool dim = false;
    bool hidden = false;
    bool strikethrough = false;
    bool continuation = false;
};

class TerminalEmulator {
public:
    static constexpr int kCols = 40;
    static constexpr int kRows = 9;

    TerminalEmulator(int cols = kCols, int rows = kRows, size_t scrollback_lines = 400);

    void reset();
    void resize(int cols, int rows);
    void process(const std::string& bytes);
    std::string take_pending_output();
    void mark_all_dirty();
    void clear_dirty();

    const TerminalCell& cell(int row, int col) const;
    std::vector<TerminalCell> line(int row) const;
    std::vector<std::vector<TerminalCell>> scrollback_snapshot(int offset) const;
    std::vector<int> dirty_rows() const;
    int cols() const;
    int rows() const;
    int cursor_row() const;
    int cursor_col() const;
    bool cursor_visible() const;
    bool application_cursor_mode() const;
    int scrollback_size() const;
    int max_scrollback_offset() const;

private:
    enum class ParserState {
        kGround,
        kEsc,
        kCsi,
        kOsc,
        kOscEsc,
        kIgnoreOne,
    };

    struct CursorState {
        int row = 0;
        int col = 0;
    };

    std::vector<TerminalCell>& active_cells();
    const std::vector<TerminalCell>& active_cells() const;
    TerminalCell& active_cell(int row, int col);
    const TerminalCell& active_cell(int row, int col) const;
    size_t cell_index(int row, int col) const;
    std::vector<TerminalCell> blank_line() const;
    void append_scrollback_line(const std::vector<TerminalCell>& line);
    void mark_dirty(int row);
    void put_char(char ch);
    void put_codepoint(uint32_t codepoint);
    void put_glyph(uint32_t codepoint, int width);
    void handle_utf8_byte(uint8_t ch);
    void clear_wide_fragment(int row, int col);
    void line_feed();
    void reverse_index();
    void carriage_return();
    void backspace();
    void tab();
    void reset_tab_stops();
    void set_tab_stop();
    void clear_tab_stop(int col);
    void clear_all_tab_stops();
    int next_tab_stop() const;
    void scroll_up(int top, int bottom, int count);
    void scroll_down(int top, int bottom, int count);
    void clear_cells(int row_start, int col_start, int row_end, int col_end);
    void clear_line(int mode);
    void clear_screen(int mode);
    void set_cursor(int row, int col);
    void move_cursor(int row_delta, int col_delta);
    void set_scroll_region(int top, int bottom);
    void save_cursor();
    void restore_cursor();
    void reset_style();
    void apply_sgr();
    void apply_mode(bool enabled);
    void apply_extended_color(bool foreground, size_t& index);
    void enter_alt_screen(bool save);
    void leave_alt_screen(bool restore);
    void clear_active_screen();
    void append_device_attributes();
    void append_secondary_device_attributes();
    void append_cursor_report(bool dec);
    void handle_escape(uint8_t ch);
    void handle_charset_select(uint8_t ch);
    void begin_csi();
    void collect_csi(uint8_t ch);
    void dispatch_csi(uint8_t final);
    int param(size_t index, int fallback) const;
    bool private_param(int value) const;

    int cols_ = kCols;
    int rows_ = kRows;
    size_t max_scrollback_lines_ = 400;
    std::vector<TerminalCell> main_cells_;
    std::vector<TerminalCell> alt_cells_;
    std::vector<bool> dirty_;
    std::vector<std::vector<TerminalCell>> scrollback_;
    std::vector<bool> tab_stops_;
    bool alt_active_ = false;
    ParserState state_ = ParserState::kGround;
    CursorState cursor_;
    CursorState saved_cursor_;
    CursorState saved_main_cursor_;
    int scroll_top_ = 0;
    int scroll_bottom_ = kRows - 1;
    bool cursor_visible_ = true;
    bool auto_wrap_ = true;
    bool wrap_pending_ = false;
    bool origin_mode_ = false;
    bool insert_mode_ = false;
    bool selecting_charset_ = false;
    bool dec_graphics_ = false;
    bool saved_dec_graphics_ = false;
    bool application_cursor_mode_ = false;
    uint32_t last_printed_codepoint_ = ' ';
    int last_printed_width_ = 1;
    TerminalCell style_;
    std::string pending_output_;
    uint32_t utf8_codepoint_ = 0;
    uint8_t utf8_remaining_ = 0;
    uint8_t utf8_expected_ = 0;
    std::vector<int> csi_params_;
    int csi_current_ = -1;
    char csi_private_ = '\0';
};

}  // namespace adv
