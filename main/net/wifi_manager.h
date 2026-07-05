#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "storage/settings_store.h"

namespace adv {

struct WifiNetwork {
    std::string ssid;
    int8_t rssi = 0;
    bool auth_required = true;
    bool saved = false;
};

enum class WifiState {
    kStopped,
    kScanning,
    kConnecting,
    kConnected,
    kFailed,
};

class WifiManager {
public:
    explicit WifiManager(SettingsStore& store);

    esp_err_t begin();
    std::vector<WifiNetwork> scan();
    esp_err_t auto_connect();
    esp_err_t connect_and_save(const WifiProfile& profile);
    esp_err_t disconnect();

    WifiState state() const;
    std::string connected_ssid() const;
    std::string last_error() const;
    bool is_connected() const;
    bool has_ipv6() const;
    bool has_global_ipv6() const;
    std::string ipv4_address() const;
    std::string ipv6_status() const;

private:
    static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    void on_wifi_event(int32_t event_id, void* event_data);
    void on_ip_event(int32_t event_id, void* event_data);
    esp_err_t connect_profile(const WifiProfile& profile, bool persist);
    bool wait_for_connection(uint32_t timeout_ms);
    bool wait_for_ipv6(uint32_t timeout_ms);

    SettingsStore& store_;
    esp_netif_t* sta_netif_ = nullptr;
    WifiState state_ = WifiState::kStopped;
    std::string connected_ssid_;
    std::string last_error_;
    bool connected_ = false;
    bool ipv6_ready_ = false;
    bool global_ipv6_ready_ = false;
    esp_ip6_addr_type_t ipv6_type_ = ESP_IP6_ADDR_IS_UNKNOWN;
};

}  // namespace adv
