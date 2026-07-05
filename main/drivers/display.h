#pragma once

#include <string>
#include <vector>

#include "terminal/terminal_emulator.h"

namespace adv {

enum class TerminalFontMode {
    kCompact,
    kReadable,
    kLarge,
};

enum class TerminalChromeMode {
    kFull,
    kCompact,
    kHidden,
};

enum class LauncherIcon {
    kTerminal,
    kWifi,
    kSsh,
    kStatus,
};

struct LauncherItem {
    LauncherIcon icon = LauncherIcon::kTerminal;
    std::string title;
    std::string subtitle;
};

struct TerminalLayout {
    TerminalFontMode mode = TerminalFontMode::kCompact;
    TerminalChromeMode chrome = TerminalChromeMode::kFull;
    int cols = TerminalEmulator::kCols;
    int rows = TerminalEmulator::kRows;
    int top = 20;
    int cell_width = 6;
    int cell_height = 12;
    int ascii_size = 1;
    int cjk_scale = 1;
    bool ascii_8x16 = false;
};

class Display {
public:
    void begin();
    void set_status_flags(bool wifi_connected, bool ssh_connected);
    void set_terminal_chrome_mode(TerminalChromeMode mode);
    TerminalLayout terminal_layout(TerminalFontMode mode) const;
    void show_status(const std::string& title, const std::vector<std::string>& lines);
    void show_menu(const std::string& title, const std::vector<std::string>& items, int selected,
                   const std::string& footer);
    void show_launcher(const std::vector<LauncherItem>& items, int selected, const std::string& footer);
    void show_text_input(const std::string& title, const std::string& label, const std::string& value, bool secret);
    void show_terminal(const std::vector<std::string>& lines, const std::string& input);
    void draw_terminal_frame();
    void draw_terminal_frame(const TerminalLayout& layout, const std::string& title = "Terminal");
    void draw_terminal_rows(const TerminalEmulator& terminal, const std::vector<int>& rows);
    void draw_terminal_rows(const TerminalEmulator& terminal, const std::vector<int>& rows,
                            const TerminalLayout& layout);
    void draw_terminal_snapshot(const std::vector<std::vector<TerminalCell>>& lines,
                                const TerminalLayout& layout);
};

}  // namespace adv
