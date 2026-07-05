#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "vterm.h"

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

enum class TerminalInputKey {
    kEnter,
    kTab,
    kBackspace,
    kEscape,
    kUp,
    kDown,
    kLeft,
    kRight,
};

class TerminalEmulator {
public:
    static constexpr int kCols = 40;
    static constexpr int kRows = 9;

    TerminalEmulator(int cols = kCols, int rows = kRows, size_t scrollback_lines = 400);
    ~TerminalEmulator();

    TerminalEmulator(const TerminalEmulator&) = delete;
    TerminalEmulator& operator=(const TerminalEmulator&) = delete;
    TerminalEmulator(TerminalEmulator&&) = delete;
    TerminalEmulator& operator=(TerminalEmulator&&) = delete;

    void reset();
    void resize(int cols, int rows);
    void process(const std::string& bytes);
    std::string take_pending_output();
    std::string encode_character(char ch);
    std::string encode_key(TerminalInputKey key);
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
    struct CursorState {
        int row = 0;
        int col = 0;
    };

    struct PackedTerminalCell {
        uint32_t codepoint = ' ';
        uint8_t fg = 7;
        uint8_t bg = 0;
        uint8_t width = 1;
        uint8_t flags = 0;
    };

    struct ScrollbackLine {
        PackedTerminalCell* cells = nullptr;
        uint8_t cols = 0;

        ScrollbackLine() = default;
        ~ScrollbackLine();
        ScrollbackLine(const ScrollbackLine&) = delete;
        ScrollbackLine& operator=(const ScrollbackLine&) = delete;
        ScrollbackLine(ScrollbackLine&& other) noexcept;
        ScrollbackLine& operator=(ScrollbackLine&& other) noexcept;

        bool assign(size_t cell_count);
        void clear();
    };

    std::vector<TerminalCell> blank_line() const;
    void init_vterm();
    void release_vterm();
    void append_scrollback_line(int cols, const VTermScreenCell* cells);
    PackedTerminalCell pack_cell(const VTermScreenCell& cell) const;
    TerminalCell unpack_cell(const PackedTerminalCell& cell) const;
    bool is_default_blank(const PackedTerminalCell& cell) const;
    TerminalCell convert_cell(const VTermScreenCell& cell) const;
    const ScrollbackLine* scrollback_line(size_t logical_index) const;
    bool ensure_scrollback_slot();
    void drop_oldest_scrollback_line();
    void clear_scrollback();
    void copy_scrollback_line(size_t logical_index, std::vector<TerminalCell>& out) const;
    void mark_dirty(int row);
    void mark_dirty_rect(VTermRect rect);
    void update_cursor(VTermPos pos, bool visible);
    void update_cursor_from_state();
    std::string capture_generated_output(size_t before);

    static void output_callback(const char* bytes, size_t len, void* user);
    static int damage_callback(VTermRect rect, void* user);
    static int moverect_callback(VTermRect dest, VTermRect src, void* user);
    static int movecursor_callback(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int settermprop_callback(VTermProp prop, VTermValue* value, void* user);
    static int bell_callback(void* user);
    static int resize_callback(int rows, int cols, void* user);
    static int sb_pushline_callback(int cols, const VTermScreenCell* cells, void* user);
    static int sb_popline_callback(int cols, VTermScreenCell* cells, void* user);
    static int sb_clear_callback(void* user);
    static int sb_pushline4_callback(int cols, const VTermScreenCell* cells, bool continuation, void* user);

    int cols_ = kCols;
    int rows_ = kRows;
    size_t max_scrollback_lines_ = 400;
    std::vector<bool> dirty_;
    std::vector<ScrollbackLine> scrollback_;
    size_t scrollback_start_ = 0;
    size_t scrollback_count_ = 0;
    CursorState cursor_;
    bool cursor_visible_ = true;
    bool alt_active_ = false;
    std::string pending_output_;
    mutable TerminalCell scratch_cell_;
    VTerm* vt_ = nullptr;
    VTermState* state_ = nullptr;
    VTermScreen* screen_ = nullptr;
};

}  // namespace adv
