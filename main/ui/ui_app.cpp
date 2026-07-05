#include "ui/ui_app.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "terminal/terminal_emulator.h"

namespace adv {
namespace {
constexpr const char* kChromeFull = "full";
constexpr const char* kChromeCompact = "compact";
constexpr const char* kChromeHidden = "hidden";

struct ThemeOption {
    TerminalTheme theme;
    const char* storage;
    const char* label;
};

struct FontOption {
    TerminalFontFace face;
    const char* storage;
    const char* label;
};

static constexpr ThemeOption kThemeOptions[] = {
    {TerminalTheme::kAdvDark, "adv_dark", "Adv Dark"},
    {TerminalTheme::kTrueBlack, "true_black", "True Black"},
    {TerminalTheme::kSolarizedDark, "solarized_dark", "Solarized Dark"},
    {TerminalTheme::kGruvboxDark, "gruvbox_dark", "Gruvbox Dark"},
    {TerminalTheme::kDracula, "dracula", "Dracula"},
    {TerminalTheme::kNord, "nord", "Nord"},
    {TerminalTheme::kTokyoNight, "tokyo_night", "Tokyo Night"},
    {TerminalTheme::kCatppuccinMocha, "catppuccin_mocha", "Catppuccin Mocha"},
    {TerminalTheme::kMonokai, "monokai", "Monokai"},
};
constexpr int kThemeOptionCount = sizeof(kThemeOptions) / sizeof(kThemeOptions[0]);

static constexpr FontOption kFontOptions[] = {
    {TerminalFontFace::kCjk12, "cjk_12", "CJK 12"},
    {TerminalFontFace::kCjk12Bold, "cjk_12_bold", "CJK 12 Bold"},
    {TerminalFontFace::kCjk10, "cjk_10", "CJK 10 Small"},
    {TerminalFontFace::kCjk14, "cjk_14", "CJK 14 Large"},
};
constexpr int kFontOptionCount = sizeof(kFontOptions) / sizeof(kFontOptions[0]);

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

std::string key_to_shell_bytes(const KeyEvent& event, TerminalEmulator& terminal)
{
    switch (event.kind) {
        case KeyKind::kCharacter:
            return (event.character >= 1 && event.character <= 126) ? terminal.encode_character(event.character)
                                                                     : std::string();
        case KeyKind::kEnter:
            return terminal.encode_key(TerminalInputKey::kEnter);
        case KeyKind::kEscape:
            return terminal.encode_key(TerminalInputKey::kEscape);
        case KeyKind::kBackspace:
        case KeyKind::kDelete:
            return terminal.encode_key(TerminalInputKey::kBackspace);
        case KeyKind::kTab:
            return terminal.encode_key(TerminalInputKey::kTab);
        case KeyKind::kUp:
            return terminal.encode_key(TerminalInputKey::kUp);
        case KeyKind::kDown:
            return terminal.encode_key(TerminalInputKey::kDown);
        case KeyKind::kRight:
            return terminal.encode_key(TerminalInputKey::kRight);
        case KeyKind::kLeft:
            return terminal.encode_key(TerminalInputKey::kLeft);
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

TerminalChromeMode parse_chrome_mode(const std::string& raw)
{
    if (raw == kChromeCompact) {
        return TerminalChromeMode::kCompact;
    }
    if (raw == kChromeHidden) {
        return TerminalChromeMode::kHidden;
    }
    return TerminalChromeMode::kFull;
}

std::string chrome_mode_storage_value(TerminalChromeMode mode)
{
    switch (mode) {
        case TerminalChromeMode::kCompact:
            return kChromeCompact;
        case TerminalChromeMode::kHidden:
            return kChromeHidden;
        case TerminalChromeMode::kFull:
        default:
            return kChromeFull;
    }
}

std::string chrome_mode_text(TerminalChromeMode mode)
{
    switch (mode) {
        case TerminalChromeMode::kCompact:
            return "Chrome: compact";
        case TerminalChromeMode::kHidden:
            return "Chrome: hidden";
        case TerminalChromeMode::kFull:
        default:
            return "Chrome: full";
    }
}

TerminalTheme parse_terminal_theme(const std::string& raw)
{
    for (const auto& option : kThemeOptions) {
        if (raw == option.storage) {
            return option.theme;
        }
    }
    return TerminalTheme::kAdvDark;
}

std::string theme_storage_value(TerminalTheme theme)
{
    for (const auto& option : kThemeOptions) {
        if (option.theme == theme) {
            return option.storage;
        }
    }
    return kThemeOptions[0].storage;
}

std::string theme_text(TerminalTheme theme)
{
    for (const auto& option : kThemeOptions) {
        if (option.theme == theme) {
            return std::string("Theme: ") + option.label;
        }
    }
    return std::string("Theme: ") + kThemeOptions[0].label;
}

TerminalFontFace parse_terminal_font_face(const std::string& raw)
{
    for (const auto& option : kFontOptions) {
        if (raw == option.storage) {
            return option.face;
        }
    }
    return TerminalFontFace::kCjk12;
}

std::string font_storage_value(TerminalFontFace face)
{
    for (const auto& option : kFontOptions) {
        if (option.face == face) {
            return option.storage;
        }
    }
    return kFontOptions[0].storage;
}

std::string font_text(TerminalFontFace face)
{
    for (const auto& option : kFontOptions) {
        if (option.face == face) {
            return std::string("Font: ") + option.label;
        }
    }
    return std::string("Font: ") + kFontOptions[0].label;
}
}  // namespace

UiApp::UiApp(Display& display, Keyboard& keyboard, SettingsStore& store, WifiManager& wifi, SshClient& ssh,
             WebServer& web)
    : display_(display), keyboard_(keyboard), store_(store), wifi_(wifi), ssh_(ssh), web_(web),
      input_(display, keyboard)
{
}

void UiApp::load_settings()
{
    if (settings_loaded_) {
        return;
    }
    terminal_chrome_mode_ = parse_chrome_mode(store_.load_terminal_chrome_mode());
    display_.set_terminal_chrome_mode(terminal_chrome_mode_);
    terminal_theme_ = parse_terminal_theme(store_.load_terminal_theme());
    display_.set_terminal_theme(terminal_theme_);
    terminal_font_face_ = parse_terminal_font_face(store_.load_terminal_font());
    display_.set_terminal_font_face(terminal_font_face_);
    settings_loaded_ = true;
}

void UiApp::refresh_status_flags()
{
    display_.set_status_flags(wifi_.is_connected(), ssh_.connected());
    display_.set_terminal_chrome_mode(terminal_chrome_mode_);
    display_.set_terminal_theme(terminal_theme_);
    display_.set_terminal_font_face(terminal_font_face_);
}

void UiApp::run()
{
    load_settings();
    int selected = 0;
    while (true) {
        SshProfile default_profile;
        std::string ssh_subtitle;
        if (has_selected_profile_) {
            ssh_subtitle = "Selected: " + profile_label(selected_profile_);
        } else {
            ssh_subtitle = load_default_ssh_profile(default_profile) ? "Default: " + profile_label(default_profile)
                                                                     : "No default SSH";
        }
        std::vector<LauncherItem> items = {
            {LauncherIcon::kTerminal, "Terminal", ssh_subtitle},
            {LauncherIcon::kWifi, "Wi-Fi", wifi_.is_connected() ? wifi_.connected_ssid() : "Network setup"},
            {LauncherIcon::kWeb, "WebUI", "Local configuration"},
            {LauncherIcon::kSsh, "SSH Profiles", "Choose or edit servers"},
            {LauncherIcon::kStatus, "Status", terminal_chrome_label()},
        };
        selected = launcher(items, selected, "Left/Right move  Enter open");
        if (selected == 0) {
            terminal_screen();
        } else if (selected == 1) {
            wifi_screen();
        } else if (selected == 2) {
            webui_screen();
        } else if (selected == 3) {
            ssh_screen();
        } else if (selected == 4) {
            status_screen();
        }
    }
}

int UiApp::launcher(const std::vector<LauncherItem>& items, int selected, const std::string& footer)
{
    if (items.empty()) {
        return -1;
    }
    selected = std::clamp(selected, 0, static_cast<int>(items.size()) - 1);
    refresh_status_flags();
    display_.show_launcher(items, selected, footer);
    while (true) {
        auto event = keyboard_.poll();
        if (!event.has_value()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if ((event->kind == KeyKind::kLeft ||
             (event->kind == KeyKind::kCharacter &&
              (event->character == ',' || event->character == 'a' || event->character == 'A' ||
               event->character == 'h' || event->character == 'H')))) {
            selected = selected == 0 ? static_cast<int>(items.size()) - 1 : selected - 1;
        } else if ((event->kind == KeyKind::kRight ||
                    (event->kind == KeyKind::kCharacter &&
                     (event->character == '/' || event->character == 'd' || event->character == 'D' ||
                      event->character == 'l' || event->character == 'L')))) {
            selected = selected == static_cast<int>(items.size()) - 1 ? 0 : selected + 1;
        } else if (event->kind == KeyKind::kEnter) {
            return selected;
        }
        refresh_status_flags();
        display_.show_launcher(items, selected, footer);
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

void UiApp::webui_screen()
{
    if (!wifi_.is_connected()) {
        pause("WebUI", {"Wi-Fi is not connected", "Open Wi-Fi first"});
        return;
    }

    esp_err_t err = web_.start();
    if (err != ESP_OK) {
        pause("WebUI", {"Start failed", esp_err_to_name(err)});
        return;
    }

    std::string shown_ip;
    auto draw_webui_status = [&]() {
        refresh_status_flags();
        shown_ip = wifi_.ipv4_address();
        display_.show_status("WebUI", {"Running",
                                        shown_ip.empty() ? "IP address unavailable" : "http://" + shown_ip + "/",
                                        "Password: " + web_.password(),
                                        "Enter/Esc stop"});
    };

    draw_webui_status();
    TickType_t last_ip_check = xTaskGetTickCount();

    while (true) {
        auto event = keyboard_.poll();
        if (event.has_value() && (event->kind == KeyKind::kEnter || event->kind == KeyKind::kEscape)) {
            break;
        }
        if (!wifi_.is_connected()) {
            break;
        }
        TickType_t now = xTaskGetTickCount();
        if ((now - last_ip_check) >= pdMS_TO_TICKS(1000)) {
            std::string ip = wifi_.ipv4_address();
            if (ip != shown_ip) {
                draw_webui_status();
            }
            last_ip_check = now;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    web_.stop();
    terminal_chrome_mode_ = parse_chrome_mode(store_.load_terminal_chrome_mode());
    terminal_theme_ = parse_terminal_theme(store_.load_terminal_theme());
    terminal_font_face_ = parse_terminal_font_face(store_.load_terminal_font());
    display_.set_terminal_chrome_mode(terminal_chrome_mode_);
    display_.set_terminal_theme(terminal_theme_);
    display_.set_terminal_font_face(terminal_font_face_);
}

void UiApp::ssh_screen()
{
    auto profiles = store_.load_ssh_profiles();
    std::string default_name = store_.load_default_ssh_profile();
    std::vector<std::string> items;
    items.push_back("Add profile");
    for (const auto& profile : profiles) {
        std::string label = profile_label(profile);
        if (!default_name.empty() && profile.name == default_name) {
            label = "* " + label;
        }
        items.push_back(label);
    }
    items.push_back("Back");

    int selected = menu("SSH Profiles", items, "* default");
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
    std::vector<std::string> actions = {"Use for terminal", "Set default", "Edit", "Delete", "Back"};
    int action = menu(profile.name, actions, profile.host);
    if (action == 0) {
        selected_profile_ = profile;
        has_selected_profile_ = true;
        pause("SSH", {"Selected for terminal", profile_label(profile)});
    } else if (action == 1) {
        has_selected_profile_ = false;
        store_.save_default_ssh_profile(profile.name);
        pause("SSH", {"Default set", profile_label(profile)});
    } else if (action == 2) {
        auto edited = edit_ssh_profile(profile);
        if (!edited.name.empty()) {
            if (edited.name != profile.name) {
                store_.delete_ssh_profile(profile.name);
                if (default_name == profile.name) {
                    store_.save_default_ssh_profile(edited.name);
                }
            }
            store_.save_ssh_profile(edited);
            if (has_selected_profile_ && selected_profile_.name == profile.name) {
                selected_profile_ = edited;
            }
            if (active_profile_.name == profile.name) {
                active_profile_ = edited;
            }
        }
    } else if (action == 3) {
        if (has_selected_profile_ && selected_profile_.name == profile.name) {
            has_selected_profile_ = false;
        }
        store_.delete_ssh_profile(profile.name);
    }
}

void UiApp::status_screen()
{
    std::vector<std::string> items = {
        "Terminal chrome",
        "Terminal theme",
        "Terminal font",
        "Connection status",
        "Back",
    };
    int selected = menu("Status", items, terminal_font_label());
    if (selected == 0) {
        std::vector<std::string> modes = {"Full", "Compact", "Hidden", "Back"};
        int chosen = menu("Terminal Chrome", modes, terminal_chrome_label());
        if (chosen == 0) {
            set_terminal_chrome_mode(TerminalChromeMode::kFull);
        } else if (chosen == 1) {
            set_terminal_chrome_mode(TerminalChromeMode::kCompact);
        } else if (chosen == 2) {
            set_terminal_chrome_mode(TerminalChromeMode::kHidden);
        }
    } else if (selected == 1) {
        std::vector<std::string> themes;
        for (const auto& option : kThemeOptions) {
            themes.push_back(option.label);
        }
        themes.push_back("Back");
        int chosen = menu("Terminal Theme", themes, terminal_theme_label());
        if (chosen >= 0 && chosen < kThemeOptionCount) {
            set_terminal_theme(kThemeOptions[chosen].theme);
        }
    } else if (selected == 2) {
        std::vector<std::string> fonts;
        for (const auto& option : kFontOptions) {
            fonts.push_back(option.label);
        }
        fonts.push_back("Back");
        int chosen = menu("Terminal Font", fonts, terminal_font_label());
        if (chosen >= 0 && chosen < kFontOptionCount) {
            set_terminal_font_face(kFontOptions[chosen].face);
        }
    } else if (selected == 3) {
        pause("Status", {wifi_state_text(wifi_), "IPv6: " + wifi_.ipv6_status(),
                         ssh_.connected() ? "SSH: connected" : "SSH: disconnected",
                         active_profile_.name.empty() ? "" : "SSH host: " + profile_label(active_profile_),
                         has_selected_profile_ ? "Selected: " + profile_label(selected_profile_) : "",
                         ssh_.last_error().empty() ? "" : "SSH err: " + ssh_.last_error(),
                         terminal_chrome_label(), terminal_theme_label(), terminal_font_label()});
    }
}

bool UiApp::find_ssh_profile(const std::string& name, SshProfile& profile)
{
    if (name.empty()) {
        return false;
    }
    auto profiles = store_.load_ssh_profiles();
    auto found = std::find_if(profiles.begin(), profiles.end(), [&](const auto& item) {
        return item.name == name;
    });
    if (found == profiles.end()) {
        return false;
    }
    profile = *found;
    return true;
}

bool UiApp::load_default_ssh_profile(SshProfile& profile)
{
    return find_ssh_profile(store_.load_default_ssh_profile(), profile);
}

bool UiApp::choose_ssh_profile(SshProfile& profile)
{
    auto profiles = store_.load_ssh_profiles();
    std::string default_name = store_.load_default_ssh_profile();
    std::vector<std::string> items;
    items.push_back("Add profile");
    for (const auto& item : profiles) {
        std::string label = profile_label(item);
        if (!default_name.empty() && item.name == default_name) {
            label = "* " + label;
        }
        items.push_back(label);
    }
    items.push_back("Back");

    int selected = menu("Choose SSH", items, "* default");
    if (selected < 0 || selected == static_cast<int>(items.size()) - 1) {
        return false;
    }
    if (selected == 0) {
        SshProfile created = edit_ssh_profile({});
        if (created.name.empty()) {
            return false;
        }
        store_.save_ssh_profile(created);
        profile = created;
        selected_profile_ = created;
        has_selected_profile_ = true;
        return true;
    }

    profile = profiles[selected - 1];
    selected_profile_ = profile;
    has_selected_profile_ = true;
    return true;
}

bool UiApp::connect_ssh_profile(const SshProfile& profile)
{
    if (!wifi_.is_connected()) {
        pause("Terminal", {"Wi-Fi is not connected", "Open Wi-Fi first"});
        return false;
    }

    refresh_status_flags();
    display_.show_status("SSH", {"Connecting", profile_label(profile)});
    if (ssh_.connect(profile, store_.load_ssh_private_key()) != ESP_OK) {
        refresh_status_flags();
        return false;
    }

    active_profile_ = profile;
    refresh_status_flags();
    return true;
}

bool UiApp::active_profile_is(const SshProfile& profile) const
{
    return ssh_.connected() && !active_profile_.name.empty() && active_profile_.name == profile.name;
}

void UiApp::terminal_screen()
{
    SshProfile profile;

    auto resolve_target = [&]() {
        if (has_selected_profile_) {
            SshProfile fresh;
            if (find_ssh_profile(selected_profile_.name, fresh)) {
                selected_profile_ = fresh;
                profile = fresh;
                return true;
            }
            has_selected_profile_ = false;
        }
        return load_default_ssh_profile(profile);
    };

    if (!resolve_target() && !ssh_.connected()) {
        std::vector<std::string> actions = {"Choose SSH profile", "Add profile", "Back"};
        int action = menu("Terminal", actions, "No default SSH");
        if (action == 0) {
            if (!choose_ssh_profile(profile)) {
                return;
            }
        } else if (action == 1) {
            profile = edit_ssh_profile({});
            if (profile.name.empty()) {
                return;
            }
            store_.save_ssh_profile(profile);
            selected_profile_ = profile;
            has_selected_profile_ = true;
        } else {
            return;
        }
    }

    while (ssh_.connected() && resolve_target() && !active_profile_is(profile)) {
        std::string current = active_profile_.name.empty() ? "Current SSH" : profile_label(active_profile_);
        std::vector<std::string> actions = {"Switch server", "Keep current", "Choose other", "Back"};
        int action = menu("Switch SSH?", actions, current + " -> " + profile_label(profile));
        if (action == 0) {
            ssh_.disconnect();
            active_profile_ = {};
            refresh_status_flags();
            break;
        }
        if (action == 1) {
            if (has_selected_profile_) {
                has_selected_profile_ = false;
            }
            profile = active_profile_;
            break;
        }
        if (action == 2) {
            if (!choose_ssh_profile(profile)) {
                return;
            }
            continue;
        }
        return;
    }

    if (!ssh_.connected()) {
        if (!wifi_.is_connected()) {
            pause("Terminal", {"Wi-Fi is not connected", "Open Wi-Fi first"});
            return;
        }
        while (!connect_ssh_profile(profile)) {
            std::vector<std::string> actions = {"Retry", "Choose SSH profile", "Back"};
            int action = menu("SSH Failed", actions, ssh_.last_error().empty() ? "Connection failed" : ssh_.last_error());
            if (action == 0) {
                continue;
            }
            if (action == 1 && choose_ssh_profile(profile)) {
                continue;
            }
            return;
        }
    } else if (!active_profile_.name.empty()) {
        profile = active_profile_;
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
    auto cursor_changed = [&]() {
        return last_cursor_row != terminal.cursor_row() || last_cursor_col != terminal.cursor_col();
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
        if ((!terminal.dirty_rows().empty() || cursor_changed()) &&
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
        std::string bytes = key_to_shell_bytes(*event, terminal);
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

void UiApp::set_terminal_chrome_mode(TerminalChromeMode mode)
{
    terminal_chrome_mode_ = mode;
    display_.set_terminal_chrome_mode(mode);
    store_.save_terminal_chrome_mode(chrome_mode_storage_value(mode));
}

void UiApp::set_terminal_theme(TerminalTheme theme)
{
    terminal_theme_ = theme;
    display_.set_terminal_theme(theme);
    store_.save_terminal_theme(theme_storage_value(theme));
}

void UiApp::set_terminal_font_face(TerminalFontFace face)
{
    terminal_font_face_ = face;
    display_.set_terminal_font_face(face);
    store_.save_terminal_font(font_storage_value(face));
}

std::string UiApp::terminal_chrome_label() const
{
    return chrome_mode_text(terminal_chrome_mode_);
}

std::string UiApp::terminal_theme_label() const
{
    return theme_text(terminal_theme_);
}

std::string UiApp::terminal_font_label() const
{
    return font_text(terminal_font_face_);
}

std::string UiApp::profile_label(const SshProfile& profile) const
{
    return server_string(profile);
}

}  // namespace adv
