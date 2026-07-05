#pragma once

#include "drivers/display.h"
#include "drivers/keyboard.h"
#include "net/wifi_manager.h"
#include "ssh/ssh_client.h"
#include "storage/settings_store.h"
#include "ui/ui_app.h"
#include "web/web_server.h"

namespace adv {

class AppController {
public:
    AppController();
    void run();

private:
    SettingsStore store_;
    Display display_;
    Keyboard keyboard_;
    WifiManager wifi_;
    SshClient ssh_;
    WebServer web_;
    UiApp ui_;
};

}  // namespace adv
