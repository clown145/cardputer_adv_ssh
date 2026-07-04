#pragma once

#include <string>
#include <vector>

namespace adv {

class TerminalEmulator;

class Display {
public:
    void begin();
    void set_status_flags(bool wifi_connected, bool ssh_connected);
    void show_status(const std::string& title, const std::vector<std::string>& lines);
    void show_menu(const std::string& title, const std::vector<std::string>& items, int selected,
                   const std::string& footer);
    void show_text_input(const std::string& title, const std::string& label, const std::string& value, bool secret);
    void show_terminal(const std::vector<std::string>& lines, const std::string& input);
    void draw_terminal_frame();
    void draw_terminal_rows(const TerminalEmulator& terminal, const std::vector<int>& rows);
};

}  // namespace adv
