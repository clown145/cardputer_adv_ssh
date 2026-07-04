#include "ui/ui_app.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "terminal/terminal_emulator.h"

namespace adv {
namespace {
std::string wifi_state_text(const WifiManager& wifi)
{
    if (wifi.is_connected()) {
        return "Wi-Fi: " + wifi.connected_ssid();
    }
    if (!wifi.last_error().empty()) {
        return "Wi-Fi: " + wifi.last_error();
    }
    return "Wi-Fi: disconnected";
}

std::string server_string(const SshProfile& profile)
{
    std::string host = profile.host;
    if (host.find(':') != std::string::npos && !(host.size() > 1 && host.front() == '[' && host.back() == ']')) {
        host = "[" + host + "]";
    }
    std::string out = profile.username + "@" + host;
    if (profile.port != 22 && profile.port != 0) {
        out += ":" + std::to_string(profile.port);
    }
    return out;
}

bool parse_server_string(const std::string& raw, SshProfile& profile)
{
    auto at = raw.find('@');
    if (at == std::string::npos || at == 0 || at == raw.size() - 1) {
        return false;
    }

    profile.username = raw.substr(0, at);
    std::string host_port = raw.substr(at + 1);
    profile.port = 22;

    if (!host_port.empty() && host_port.front() == '[') {
        auto close = host_port.find(']');
        if (close == std::string::npos || close == 1) {
            return false;
        }
        profile.host = host_port.substr(1, close - 1);
        if (close + 1 < host_port.size()) {
            if (host_port[close + 1] != ':') {
                return false;
            }
            profile.port = static_cast<uint16_t>(std::atoi(host_port.substr(close + 2).c_str()));
        }
    } else {
        size_t first_colon = host_port.find(':');
        size_t last_colon = host_port.rfind(':');
        if (first_colon != std::string::npos && first_colon == last_colon) {
            profile.host = host_port.substr(0, first_colon);
            profile.port = static_cast<uint16_t>(std::atoi(host_port.substr(first_colon + 1).c_str()));
        } else {
            profile.host = host_port;
        }
    }

    if (profile.host.empty()) {
        return false;
    }
    if (profile.port == 0) {
        profile.port = 22;
    }
    profile.name = raw;
    return true;
}

std::string key_to_shell_bytes(const KeyEvent& event)
{
    switch (event.kind) {
        case KeyKind::kCharacter:
            return event.character >= 32 && event.character <= 126 ? std::string(1, event.character) : std::string();
        case KeyKind::kEnter:
            return "\r";
        case KeyKind::kEscape:
            return "\x1B";
        case KeyKind::kBackspace:
        case KeyKind::kDelete:
            return "\x7F";
        case KeyKind::kTab:
            return "\t";
        case KeyKind::kUp:
            return "\x1B[A";
        case KeyKind::kDown:
            return "\x1B[B";
        case KeyKind::kRight:
            return "\x1B[C";
        case KeyKind::kLeft:
            return "\x1B[D";
        default:
            return {};
    }
}
}  // namespace

UiApp::UiApp(Display& display, Keyboard& keyboard, SettingsStore& store, WifiManager& wifi, SshClient& ssh)
    : display_(display), keyboard_(keyboard), store_(store), wifi_(wifi), ssh_(ssh), input_(display, keyboard)
{
}

void UiApp::refresh_status_flags()
{
    display_.set_status_flags(wifi_.is_connected(), ssh_.connected());
}

void UiApp::run()
{
    while (true) {
        std::vector<std::string> items = {
            "Wi-Fi",
            "SSH profiles",
            "Terminal",
            "Status",
        };
        int selected = menu("Cardputer-Adv SSH", items, "Arrows move  Enter select");
        if (selected == 0) {
            wifi_screen();
        } else if (selected == 1) {
            ssh_screen();
        } else if (selected == 2) {
            terminal_screen();
        } else if (selected == 3) {
            pause("Status", {wifi_state_text(wifi_), "IPv6: " + wifi_.ipv6_status(),
                             ssh_.connected() ? "SSH: connected" : "SSH: disconnected",
                             ssh_.last_error().empty() ? "" : "SSH err: " + ssh_.last_error()});
        }
    }
}

int UiApp::menu(const std::string& title, const std::vector<std::string>& items, const std::string& footer)
{
    int selected = 0;
    refresh_status_flags();
    display_.show_menu(title, items, selected, footer);
    while (true) {
        auto event = keyboard_.poll();
        if (!event.has_value()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if ((event->kind == KeyKind::kUp ||
             (event->kind == KeyKind::kCharacter && event->character == ';')) &&
            selected > 0) {
            selected--;
        } else if ((event->kind == KeyKind::kDown ||
                    (event->kind == KeyKind::kCharacter && event->character == '.')) &&
                   selected < static_cast<int>(items.size()) - 1) {
            selected++;
        } else if (event->kind == KeyKind::kCharacter &&
                   (event->character == 'w' || event->character == 'W' || event->character == 'k' ||
                    event->character == 'K') &&
                   selected > 0) {
            selected--;
        } else if (event->kind == KeyKind::kCharacter &&
                   (event->character == 's' || event->character == 'S' || event->character == 'j' ||
                    event->character == 'J') &&
                   selected < static_cast<int>(items.size()) - 1) {
            selected++;
        } else if (event->kind == KeyKind::kEnter) {
            return selected;
        } else if (event->kind == KeyKind::kRight ||
                   (event->kind == KeyKind::kCharacter && event->character == '/')) {
            return selected;
        } else if (event->kind == KeyKind::kLeft ||
                   (event->kind == KeyKind::kCharacter && event->character == ',')) {
            return -1;
        } else if (event->kind == KeyKind::kEscape ||
                   (event->kind == KeyKind::kCharacter &&
                    (event->character == 'q' || event->character == 'Q' || event->character == '`'))) {
            return -1;
        }
        refresh_status_flags();
        display_.show_menu(title, items, selected, footer);
    }
}

void UiApp::pause(const std::string& title, const std::vector<std::string>& lines)
{
    refresh_status_flags();
    display_.show_status(title, lines);
    while (true) {
        auto event = keyboard_.poll();
        if (event.has_value() && (event->kind == KeyKind::kEnter || event->kind == KeyKind::kEscape)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void UiApp::wifi_screen()
{
    refresh_status_flags();
    display_.show_status("Wi-Fi", {"Scanning..."});
    auto networks = wifi_.scan();
    std::vector<std::string> items;
    for (const auto& network : networks) {
        items.push_back((network.saved ? "* " : "  ") + network.ssid + "  " + std::to_string(network.rssi) + "dBm");
    }
    items.push_back("Forget saved network");
    items.push_back("Back");
    int selected = menu("Wi-Fi Scan", items, "* saved");
    if (selected < 0 || selected >= static_cast<int>(networks.size())) {
        if (selected == static_cast<int>(networks.size())) {
            auto profiles = store_.load_wifi_profiles();
            std::vector<std::string> saved;
            for (const auto& profile : profiles) {
                saved.push_back(profile.ssid);
            }
            if (!saved.empty()) {
                int forget = menu("Forget Wi-Fi", saved, "Enter forget  Esc cancel");
                if (forget >= 0 && forget < static_cast<int>(profiles.size())) {
                    store_.forget_wifi_profile(profiles[forget].ssid);
                }
            }
        }
        return;
    }

    WifiProfile profile;
    profile.ssid = networks[selected].ssid;
    auto password = input_.prompt("Wi-Fi Password", profile.ssid, "", true);
    if (!password.has_value()) {
        return;
    }
    profile.password = *password;
    display_.show_status("Wi-Fi", {"Connecting to", profile.ssid});
    esp_err_t err = wifi_.connect_and_save(profile);
    refresh_status_flags();
    if (err == ESP_OK) {
        pause("Wi-Fi", {"Connected", profile.ssid});
    } else {
        pause("Wi-Fi", {"Failed", wifi_.last_error()});
    }
}

void UiApp::ssh_screen()
{
    auto profiles = store_.load_ssh_profiles();
    std::vector<std::string> items;
    items.push_back("Add profile");
    for (const auto& profile : profiles) {
        items.push_back(profile_label(profile));
    }
    items.push_back("Back");

    int selected = menu("SSH Profiles", items, "Enter connect/edit");
    if (selected < 0 || selected == static_cast<int>(items.size()) - 1) {
        return;
    }
    if (selected == 0) {
        SshProfile profile = edit_ssh_profile({});
        if (!profile.name.empty()) {
            store_.save_ssh_profile(profile);
        }
        return;
    }

    SshProfile profile = profiles[selected - 1];
    std::vector<std::string> actions = {"Connect", "Edit", "Delete", "Back"};
    int action = menu(profile.name, actions, profile.host);
    if (action == 0) {
        if (!wifi_.is_connected()) {
            pause("SSH", {"Wi-Fi is not connected"});
            return;
        }
        display_.show_status("SSH", {"Connecting", profile.host});
        if (ssh_.connect_password(profile) == ESP_OK) {
            active_profile_ = profile;
            pause("SSH", {"Connected", profile_label(profile)});
        } else {
            pause("SSH", {"Failed", ssh_.last_error()});
        }
    } else if (action == 1) {
        auto edited = edit_ssh_profile(profile);
        if (!edited.name.empty()) {
            store_.save_ssh_profile(edited);
        }
    } else if (action == 2) {
        store_.delete_ssh_profile(profile.name);
    }
}

void UiApp::terminal_screen()
{
    if (!ssh_.connected()) {
        pause("Terminal", {"SSH is not connected", "Open SSH profiles first"});
        return;
    }

    if (ssh_.open_shell(TerminalEmulator::kCols, TerminalEmulator::kRows) != ESP_OK) {
        pause("Terminal", {"Shell failed", ssh_.last_error()});
        return;
    }

    TerminalEmulator terminal;
    terminal.mark_all_dirty();
    refresh_status_flags();
    display_.draw_terminal_frame();
    display_.draw_terminal_rows(terminal, terminal.dirty_rows());
    terminal.clear_dirty();

    int last_cursor_row = terminal.cursor_row();
    int last_cursor_col = terminal.cursor_col();
    TickType_t last_escape_tick = 0;

    while (true) {
        std::string output;
        if (ssh_.read_shell(output, 0) != ESP_OK) {
            terminal.process("\r\nERR: " + ssh_.last_error() + "\r\n");
            display_.draw_terminal_rows(terminal, terminal.dirty_rows());
            vTaskDelay(pdMS_TO_TICKS(1200));
            return;
        }
        if (!output.empty()) {
            terminal.process(output);
        }

        auto dirty = terminal.dirty_rows();
        if (last_cursor_row != terminal.cursor_row() || last_cursor_col != terminal.cursor_col()) {
            dirty.push_back(last_cursor_row);
            dirty.push_back(terminal.cursor_row());
            last_cursor_row = terminal.cursor_row();
            last_cursor_col = terminal.cursor_col();
        }
        if (!dirty.empty()) {
            std::sort(dirty.begin(), dirty.end());
            dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
            refresh_status_flags();
            display_.draw_terminal_rows(terminal, dirty);
            terminal.clear_dirty();
        }

        auto event = keyboard_.poll();
        if (!event.has_value()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (event->kind == KeyKind::kEscape) {
            TickType_t now = xTaskGetTickCount();
            if (last_escape_tick != 0 && (now - last_escape_tick) < pdMS_TO_TICKS(650)) {
                return;
            }
            last_escape_tick = now;
        } else {
            last_escape_tick = 0;
        }
        std::string bytes = key_to_shell_bytes(*event);
        if (!bytes.empty() && ssh_.write_shell(bytes) != ESP_OK) {
            terminal.process("\r\nERR: " + ssh_.last_error() + "\r\n");
        }
    }
}

SshProfile UiApp::edit_ssh_profile(const SshProfile& initial)
{
    SshProfile profile = initial;
    auto server = input_.prompt("SSH Profile", "user@host:port", initial.host.empty() ? "" : server_string(initial));
    if (!server.has_value() || server->empty()) {
        return {};
    }
    if (!parse_server_string(*server, profile)) {
        pause("SSH Profile", {"Invalid server", "Use user@host or user@host:port", "IPv6: user@[addr]:22"});
        return {};
    }
    auto password = input_.prompt("SSH Profile", "Password", profile.password, true);
    if (!password.has_value()) {
        return {};
    }
    profile.password = *password;
    return profile;
}

std::string UiApp::profile_label(const SshProfile& profile) const
{
    return server_string(profile);
}

}  // namespace adv
