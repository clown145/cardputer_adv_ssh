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

std::string key_to_shell_bytes(const KeyEvent& event, bool application_cursor)
{
    const char* prefix = application_cursor ? "\x1BO" : "\x1B[";
    switch (event.kind) {
        case KeyKind::kCharacter:
            return (event.character >= 1 && event.character <= 126) ? std::string(1, event.character) : std::string();
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
            return std::string(prefix) + "A";
        case KeyKind::kDown:
            return std::string(prefix) + "B";
        case KeyKind::kRight:
            return std::string(prefix) + "C";
        case KeyKind::kLeft:
            return std::string(prefix) + "D";
        default:
            return {};
    }
}

TerminalFontMode adjusted_font_mode(TerminalFontMode mode, int delta)
{
    int value = 0;
    switch (mode) {
        case TerminalFontMode::kReadable:
            value = 1;
            break;
        case TerminalFontMode::kLarge:
            value = 2;
            break;
        case TerminalFontMode::kCompact:
        default:
            value = 0;
            break;
    }
    value = std::clamp(value + delta, 0, 2);
    if (value == 2) {
        return TerminalFontMode::kLarge;
    }
    if (value == 1) {
        return TerminalFontMode::kReadable;
    }
    return TerminalFontMode::kCompact;
}

bool is_review_up_key(const KeyEvent& event)
{
    return event.kind == KeyKind::kUp || (event.kind == KeyKind::kCharacter && event.character == ';');
}

bool is_review_down_key(const KeyEvent& event)
{
    return event.kind == KeyKind::kDown || (event.kind == KeyKind::kCharacter && event.character == '.');
}

int review_font_delta(const KeyEvent& event)
{
    if (event.kind == KeyKind::kLeft || (event.kind == KeyKind::kCharacter && event.character == ',')) {
        return -1;
    }
    if (event.kind == KeyKind::kRight || (event.kind == KeyKind::kCharacter && event.character == '/')) {
        return 1;
    }
    return 0;
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

    TerminalFontMode font_mode = TerminalFontMode::kCompact;
    TerminalLayout layout = display_.terminal_layout(font_mode);

    if (ssh_.open_shell(layout.cols, layout.rows) != ESP_OK) {
        pause("Terminal", {"Shell failed", ssh_.last_error()});
        return;
    }

    TerminalEmulator terminal(layout.cols, layout.rows, 300);
    terminal.mark_all_dirty();
    refresh_status_flags();

    int last_cursor_row = terminal.cursor_row();
    int last_cursor_col = terminal.cursor_col();
    TickType_t last_escape_tick = 0;
    TickType_t last_draw_tick = 0;
    bool review_mode = false;
    int scroll_offset = 0;

    auto live_title = [&]() {
        return "Terminal " + std::to_string(layout.cols) + "x" + std::to_string(layout.rows);
    };
    auto review_title = [&]() {
        return "Review " + std::to_string(scroll_offset) + "/" +
               std::to_string(terminal.max_scrollback_offset());
    };

    auto draw_dirty = [&]() {
        if (review_mode) {
            return;
        }
        auto dirty = terminal.dirty_rows();
        if (last_cursor_row != terminal.cursor_row() || last_cursor_col != terminal.cursor_col()) {
            dirty.push_back(last_cursor_row);
            dirty.push_back(terminal.cursor_row());
            last_cursor_row = terminal.cursor_row();
            last_cursor_col = terminal.cursor_col();
        }
        if (dirty.empty()) {
            return;
        }
        std::sort(dirty.begin(), dirty.end());
        dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
        refresh_status_flags();
        display_.draw_terminal_rows(terminal, dirty, layout);
        terminal.clear_dirty();
        last_draw_tick = xTaskGetTickCount();
    };
    auto draw_review = [&]() {
        refresh_status_flags();
        display_.draw_terminal_frame(layout, review_title());
        display_.draw_terminal_snapshot(terminal.scrollback_snapshot(scroll_offset), layout);
        last_draw_tick = xTaskGetTickCount();
    };
    auto redraw_live = [&]() {
        refresh_status_flags();
        display_.draw_terminal_frame(layout, live_title());
        terminal.mark_all_dirty();
        draw_dirty();
    };
    auto apply_layout = [&](TerminalFontMode next_mode) {
        TerminalLayout next_layout = display_.terminal_layout(next_mode);
        if (next_layout.cols == layout.cols && next_layout.rows == layout.rows &&
            next_layout.cell_width == layout.cell_width && next_layout.cell_height == layout.cell_height) {
            return;
        }
        font_mode = next_mode;
        layout = next_layout;
        terminal.resize(layout.cols, layout.rows);
        last_cursor_row = terminal.cursor_row();
        last_cursor_col = terminal.cursor_col();
        scroll_offset = std::min(scroll_offset, terminal.max_scrollback_offset());
        if (ssh_.resize_shell(layout.cols, layout.rows) != ESP_OK) {
            terminal.process("\r\nWARN: " + ssh_.last_error() + "\r\n");
        }
        if (review_mode) {
            draw_review();
        } else {
            redraw_live();
        }
    };
    auto flush_terminal_reply = [&]() {
        std::string reply = terminal.take_pending_output();
        if (!reply.empty()) {
            ssh_.write_shell(reply);
        }
    };

    redraw_live();

    while (true) {
        bool got_output = false;
        std::string output;
        for (int i = 0; i < 4; ++i) {
            if (ssh_.read_shell_chunk(output, 1024) != ESP_OK) {
                terminal.process("\r\nERR: " + ssh_.last_error() + "\r\n");
                draw_dirty();
                vTaskDelay(pdMS_TO_TICKS(1200));
                return;
            }
            if (output.empty()) {
                break;
            }
            got_output = true;
            int scrollback_before = terminal.scrollback_size();
            terminal.process(output);
            int scrollback_after = terminal.scrollback_size();
            if (review_mode && scrollback_after > scrollback_before) {
                scroll_offset += scrollback_after - scrollback_before;
                scroll_offset = std::min(scroll_offset, terminal.max_scrollback_offset());
            }
            flush_terminal_reply();
        }

        TickType_t now = xTaskGetTickCount();
        if (!terminal.dirty_rows().empty() &&
            (!got_output || last_draw_tick == 0 || (now - last_draw_tick) >= pdMS_TO_TICKS(50))) {
            draw_dirty();
        }

        auto event = keyboard_.poll();
        if (!event.has_value()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (review_mode) {
            if (event->kind == KeyKind::kEscape) {
                TickType_t now = xTaskGetTickCount();
                if (last_escape_tick != 0 && (now - last_escape_tick) < pdMS_TO_TICKS(650)) {
                    return;
                }
                review_mode = false;
                scroll_offset = 0;
                last_escape_tick = now;
                redraw_live();
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            last_escape_tick = 0;
            bool redraw = false;
            if (event->kind == KeyKind::kEnter) {
                review_mode = false;
                scroll_offset = 0;
                redraw_live();
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            if (is_review_up_key(*event)) {
                scroll_offset = std::min(scroll_offset + layout.rows, terminal.max_scrollback_offset());
                redraw = true;
            } else if (is_review_down_key(*event)) {
                scroll_offset = std::max(0, scroll_offset - layout.rows);
                redraw = true;
            } else {
                int font_delta = review_font_delta(*event);
                if (font_delta != 0) {
                    apply_layout(adjusted_font_mode(font_mode, font_delta));
                    vTaskDelay(pdMS_TO_TICKS(1));
                    continue;
                }
            }
            if (redraw) {
                draw_review();
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (event->kind == KeyKind::kEscape) {
            TickType_t now = xTaskGetTickCount();
            if (last_escape_tick != 0 && (now - last_escape_tick) < pdMS_TO_TICKS(650)) {
                return;
            }
            last_escape_tick = now;
            if (terminal.max_scrollback_offset() > 0) {
                review_mode = true;
                scroll_offset = 0;
                draw_review();
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
        } else {
            last_escape_tick = 0;
        }
        std::string bytes = key_to_shell_bytes(*event, terminal.application_cursor_mode());
        if (!bytes.empty() && ssh_.write_shell(bytes) != ESP_OK) {
            terminal.process("\r\nERR: " + ssh_.last_error() + "\r\n");
        } else if (bytes.size() == 1 && bytes[0] == '\x03') {
            ssh_.send_signal("INT");
        }
        vTaskDelay(pdMS_TO_TICKS(got_output ? 1 : 10));
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
