#pragma once

#include <string>
#include <vector>

#include "drivers/display.h"
#include "drivers/keyboard.h"
#include "net/wifi_manager.h"
#include "ssh/ssh_client.h"
#include "storage/settings_store.h"
#include "ui/text_input.h"

namespace adv {

class UiApp {
public:
    UiApp(Display& display, Keyboard& keyboard, SettingsStore& store, WifiManager& wifi, SshClient& ssh);
    void run();

private:
    void load_settings();
    void refresh_status_flags();
    int launcher(const std::vector<LauncherItem>& items, int selected, const std::string& footer);
    int menu(const std::string& title, const std::vector<std::string>& items, const std::string& footer);
    void pause(const std::string& title, const std::vector<std::string>& lines);
    void wifi_screen();
    void ssh_screen();
    void status_screen();
    void terminal_screen();
    SshProfile edit_ssh_profile(const SshProfile& initial);
    bool find_ssh_profile(const std::string& name, SshProfile& profile);
    bool load_default_ssh_profile(SshProfile& profile);
    bool choose_ssh_profile(SshProfile& profile);
    bool connect_ssh_profile(const SshProfile& profile);
    bool active_profile_is(const SshProfile& profile) const;
    void set_terminal_chrome_mode(TerminalChromeMode mode);
    std::string terminal_chrome_label() const;
    std::string profile_label(const SshProfile& profile) const;

    Display& display_;
    Keyboard& keyboard_;
    SettingsStore& store_;
    WifiManager& wifi_;
    SshClient& ssh_;
    TextInput input_;
    SshProfile active_profile_;
    SshProfile selected_profile_;
    bool has_selected_profile_ = false;
    TerminalChromeMode terminal_chrome_mode_ = TerminalChromeMode::kFull;
    bool settings_loaded_ = false;
};

}  // namespace adv
