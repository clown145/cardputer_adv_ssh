#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"

namespace adv {

struct WifiProfile {
    std::string ssid;
    std::string password;
    int32_t priority = 0;
};

struct SshProfile {
    std::string name;
    std::string host;
    uint16_t port = 22;
    std::string username;
    std::string password;
};

class SettingsStore {
public:
    esp_err_t begin();

    std::vector<WifiProfile> load_wifi_profiles();
    esp_err_t save_wifi_profile(const WifiProfile& profile);
    esp_err_t forget_wifi_profile(const std::string& ssid);

    std::vector<SshProfile> load_ssh_profiles();
    esp_err_t save_ssh_profile(const SshProfile& profile);
    esp_err_t delete_ssh_profile(const std::string& name);

private:
    static constexpr int kMaxWifiProfiles = 8;
    static constexpr int kMaxSshProfiles = 8;

    std::vector<std::string> load_index(const char* key, int max_count);
    esp_err_t save_index(const char* key, const std::vector<std::string>& values);
    std::string get_string(const char* ns, const std::string& key);
    esp_err_t set_string(const char* ns, const std::string& key, const std::string& value);
    esp_err_t erase_namespace_key(const char* ns, const std::string& key);
};

}  // namespace adv
