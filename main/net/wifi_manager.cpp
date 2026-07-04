#include "net/wifi_manager.h"

#include <algorithm>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace adv {
namespace {
constexpr const char* TAG = "wifi";
constexpr int kScanLimit = 24;

std::string auth_to_error(wifi_err_reason_t reason)
{
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
            return "auth failed";
        case WIFI_REASON_NO_AP_FOUND:
            return "network not found";
        case WIFI_REASON_ASSOC_FAIL:
            return "association failed";
        default:
            return "connection failed";
    }
}
}  // namespace

WifiManager::WifiManager(SettingsStore& store) : store_(store) {}

esp_err_t WifiManager::begin()
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    sta_netif_ = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::event_handler,
                                                            this, nullptr),
                        TAG, "wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::event_handler,
                                                            this, nullptr),
                        TAG, "ip handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_GOT_IP6, &WifiManager::event_handler,
                                                            this, nullptr),
                        TAG, "ip6 handler");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi sta mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    state_ = WifiState::kStopped;
    return ESP_OK;
}

std::vector<WifiNetwork> WifiManager::scan()
{
    state_ = WifiState::kScanning;
    last_error_.clear();

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    if (esp_wifi_scan_start(&scan_config, true) != ESP_OK) {
        last_error_ = "scan failed";
        state_ = WifiState::kFailed;
        return {};
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > kScanLimit) {
        count = kScanLimit;
    }

    std::vector<wifi_ap_record_t> records(count);
    if (count > 0) {
        esp_wifi_scan_get_ap_records(&count, records.data());
    }

    auto saved_profiles = store_.load_wifi_profiles();
    std::vector<WifiNetwork> networks;
    for (const auto& record : records) {
        WifiNetwork network;
        network.ssid = reinterpret_cast<const char*>(record.ssid);
        network.rssi = record.rssi;
        network.auth_required = record.authmode != WIFI_AUTH_OPEN;
        network.saved = std::any_of(saved_profiles.begin(), saved_profiles.end(), [&](const auto& profile) {
            return profile.ssid == network.ssid;
        });
        if (!network.ssid.empty()) {
            networks.push_back(network);
        }
    }

    std::sort(networks.begin(), networks.end(), [](const auto& a, const auto& b) {
        return a.rssi > b.rssi;
    });
    state_ = connected_ ? WifiState::kConnected : WifiState::kStopped;
    return networks;
}

esp_err_t WifiManager::auto_connect()
{
    auto networks = scan();
    auto profiles = store_.load_wifi_profiles();
    for (const auto& network : networks) {
        auto match = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
            return profile.ssid == network.ssid;
        });
        if (match == profiles.end()) {
            continue;
        }
        ESP_LOGI(TAG, "auto connecting to %s", match->ssid.c_str());
        if (connect_profile(*match, false) == ESP_OK) {
            return ESP_OK;
        }
    }
    last_error_ = "no saved network available";
    state_ = WifiState::kFailed;
    return ESP_FAIL;
}

esp_err_t WifiManager::connect_and_save(const WifiProfile& profile)
{
    esp_err_t err = connect_profile(profile, true);
    if (err == ESP_OK) {
        WifiProfile saved = profile;
        saved.priority++;
        store_.save_wifi_profile(saved);
    }
    return err;
}

esp_err_t WifiManager::disconnect()
{
    connected_ = false;
    connected_ssid_.clear();
    state_ = WifiState::kStopped;
    return esp_wifi_disconnect();
}

WifiState WifiManager::state() const
{
    return state_;
}

std::string WifiManager::connected_ssid() const
{
    return connected_ssid_;
}

std::string WifiManager::last_error() const
{
    return last_error_;
}

bool WifiManager::is_connected() const
{
    return connected_;
}

bool WifiManager::has_ipv6() const
{
    return ipv6_ready_;
}

bool WifiManager::has_global_ipv6() const
{
    return global_ipv6_ready_;
}

std::string WifiManager::ipv6_status() const
{
    if (!ipv6_ready_) {
        return "not ready";
    }
    switch (ipv6_type_) {
        case ESP_IP6_ADDR_IS_GLOBAL:
            return "global";
        case ESP_IP6_ADDR_IS_UNIQUE_LOCAL:
            return "unique-local";
        case ESP_IP6_ADDR_IS_LINK_LOCAL:
            return "link-local only";
        case ESP_IP6_ADDR_IS_SITE_LOCAL:
            return "site-local";
        case ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6:
            return "v4-mapped";
        default:
            return "unknown";
    }
}

void WifiManager::event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    auto* self = static_cast<WifiManager*>(arg);
    if (event_base == WIFI_EVENT) {
        self->on_wifi_event(event_id, event_data);
    } else if (event_base == IP_EVENT) {
        self->on_ip_event(event_id, event_data);
    }
}

void WifiManager::on_wifi_event(int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        if (sta_netif_ != nullptr) {
            esp_netif_create_ip6_linklocal(sta_netif_);
        }
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        connected_ = false;
        ipv6_ready_ = false;
        global_ipv6_ready_ = false;
        ipv6_type_ = ESP_IP6_ADDR_IS_UNKNOWN;
        connected_ssid_.clear();
        auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        last_error_ = auth_to_error(static_cast<wifi_err_reason_t>(event->reason));
        state_ = WifiState::kFailed;
        ESP_LOGW(TAG, "wifi disconnected: %s", last_error_.c_str());
    }
}

void WifiManager::on_ip_event(int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        connected_ = true;
        state_ = WifiState::kConnected;
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
    } else if (event_id == IP_EVENT_GOT_IP6) {
        auto* event = static_cast<ip_event_got_ip6_t*>(event_data);
        auto type = esp_netif_ip6_get_addr_type(&event->ip6_info.ip);
        ipv6_ready_ = true;
        ipv6_type_ = type;
        global_ipv6_ready_ = type == ESP_IP6_ADDR_IS_GLOBAL || type == ESP_IP6_ADDR_IS_UNIQUE_LOCAL;
        ESP_LOGI(TAG, "got ipv6: " IPV6STR " type=%d", IPV62STR(event->ip6_info.ip), static_cast<int>(type));
    }
}

esp_err_t WifiManager::connect_profile(const WifiProfile& profile, bool persist)
{
    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), profile.ssid.c_str(), sizeof(wifi_config.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), profile.password.c_str(),
                 sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = profile.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    state_ = WifiState::kConnecting;
    connected_ = false;
    ipv6_ready_ = false;
    global_ipv6_ready_ = false;
    ipv6_type_ = ESP_IP6_ADDR_IS_UNKNOWN;
    connected_ssid_.clear();
    last_error_.clear();

    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(persist ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "wifi connect");

    if (!wait_for_connection(20000)) {
        if (last_error_.empty()) {
            last_error_ = "connection timed out";
        }
        state_ = WifiState::kFailed;
        return ESP_ERR_TIMEOUT;
    }
    connected_ssid_ = profile.ssid;
    wait_for_ipv6(7000);
    return ESP_OK;
}

bool WifiManager::wait_for_connection(uint32_t timeout_ms)
{
    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    while ((xTaskGetTickCount() - started) < timeout) {
        if (connected_) {
            return true;
        }
        if (state_ == WifiState::kFailed && !last_error_.empty()) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

bool WifiManager::wait_for_ipv6(uint32_t timeout_ms)
{
    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    while ((xTaskGetTickCount() - started) < timeout) {
        if (ipv6_ready_) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

}  // namespace adv
