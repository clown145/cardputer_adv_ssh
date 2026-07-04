#include "app/app_controller.h"

#include "esp_err.h"
#include "esp_log.h"

namespace adv {
namespace {
constexpr const char* TAG = "app";
}

AppController::AppController() : wifi_(store_), ui_(display_, keyboard_, store_, wifi_, ssh_) {}

void AppController::run()
{
    display_.begin();
    display_.show_status("Boot", {"Initializing storage..."});
    ESP_ERROR_CHECK(store_.begin());

    display_.show_status("Boot", {"Initializing keyboard..."});
    if (!keyboard_.begin()) {
        display_.show_status("Boot", {"Keyboard init failed", "Serial monitor only"});
        ESP_LOGW(TAG, "keyboard init failed");
    }

    display_.show_status("Boot", {"Starting Wi-Fi..."});
    ESP_ERROR_CHECK(wifi_.begin());

    display_.show_status("Boot", {"Trying saved Wi-Fi..."});
    wifi_.auto_connect();

    ui_.run();
}

}  // namespace adv
