#include "storage/settings_store.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace adv {
namespace {
constexpr const char* TAG = "settings";
constexpr const char* NS_WIFI = "wifi_profiles";
constexpr const char* NS_SSH = "ssh_profiles";
constexpr const char* KEY_WIFI_INDEX = "wifi_index";
constexpr const char* KEY_SSH_INDEX = "ssh_index";

std::string make_key(const std::string& prefix, const std::string& id)
{
    uint32_t hash = 2166136261u;
    for (char ch : id) {
        hash ^= static_cast<uint8_t>(ch);
        hash *= 16777619u;
    }
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%s_%08" PRIx32, prefix.c_str(), hash);
    return buffer;
}

std::vector<std::string> split_index(const std::string& raw)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start < raw.size()) {
        size_t end = raw.find('\n', start);
        if (end == std::string::npos) {
            end = raw.size();
        }
        if (end > start) {
            out.push_back(raw.substr(start, end - start));
        }
        start = end + 1;
    }
    return out;
}

std::string join_index(const std::vector<std::string>& values)
{
    std::string out;
    for (const auto& value : values) {
        out += value;
        out += '\n';
    }
    return out;
}
}  // namespace

esp_err_t SettingsStore::begin()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase nvs");
        err = nvs_flash_init();
    }
    return err;
}

std::vector<WifiProfile> SettingsStore::load_wifi_profiles()
{
    std::vector<WifiProfile> profiles;
    for (const auto& ssid : load_index(KEY_WIFI_INDEX, kMaxWifiProfiles)) {
        WifiProfile profile;
        profile.ssid = ssid;
        profile.password = get_string(NS_WIFI, make_key("p", ssid));
        std::string priority = get_string(NS_WIFI, make_key("r", ssid));
        profile.priority = priority.empty() ? 0 : std::atoi(priority.c_str());
        profiles.push_back(profile);
    }
    std::sort(profiles.begin(), profiles.end(), [](const auto& a, const auto& b) {
        return a.priority > b.priority;
    });
    return profiles;
}

esp_err_t SettingsStore::save_wifi_profile(const WifiProfile& profile)
{
    auto index = load_index(KEY_WIFI_INDEX, kMaxWifiProfiles);
    index.erase(std::remove(index.begin(), index.end(), profile.ssid), index.end());
    index.insert(index.begin(), profile.ssid);
    if (index.size() > kMaxWifiProfiles) {
        index.resize(kMaxWifiProfiles);
    }
    ESP_RETURN_ON_ERROR(set_string(NS_WIFI, make_key("p", profile.ssid), profile.password), TAG, "save wifi password");
    ESP_RETURN_ON_ERROR(set_string(NS_WIFI, make_key("r", profile.ssid), std::to_string(profile.priority)), TAG,
                        "save wifi priority");
    return save_index(KEY_WIFI_INDEX, index);
}

esp_err_t SettingsStore::forget_wifi_profile(const std::string& ssid)
{
    auto index = load_index(KEY_WIFI_INDEX, kMaxWifiProfiles);
    index.erase(std::remove(index.begin(), index.end(), ssid), index.end());
    erase_namespace_key(NS_WIFI, make_key("p", ssid));
    erase_namespace_key(NS_WIFI, make_key("r", ssid));
    return save_index(KEY_WIFI_INDEX, index);
}

std::vector<SshProfile> SettingsStore::load_ssh_profiles()
{
    std::vector<SshProfile> profiles;
    for (const auto& name : load_index(KEY_SSH_INDEX, kMaxSshProfiles)) {
        SshProfile profile;
        profile.name = name;
        profile.host = get_string(NS_SSH, make_key("h", name));
        std::string port = get_string(NS_SSH, make_key("o", name));
        profile.port = port.empty() ? 22 : static_cast<uint16_t>(std::atoi(port.c_str()));
        profile.username = get_string(NS_SSH, make_key("u", name));
        profile.password = get_string(NS_SSH, make_key("p", name));
        profiles.push_back(profile);
    }
    return profiles;
}

esp_err_t SettingsStore::save_ssh_profile(const SshProfile& profile)
{
    auto index = load_index(KEY_SSH_INDEX, kMaxSshProfiles);
    index.erase(std::remove(index.begin(), index.end(), profile.name), index.end());
    index.insert(index.begin(), profile.name);
    if (index.size() > kMaxSshProfiles) {
        index.resize(kMaxSshProfiles);
    }
    ESP_RETURN_ON_ERROR(set_string(NS_SSH, make_key("h", profile.name), profile.host), TAG, "save ssh host");
    ESP_RETURN_ON_ERROR(set_string(NS_SSH, make_key("o", profile.name), std::to_string(profile.port)), TAG,
                        "save ssh port");
    ESP_RETURN_ON_ERROR(set_string(NS_SSH, make_key("u", profile.name), profile.username), TAG, "save ssh user");
    ESP_RETURN_ON_ERROR(set_string(NS_SSH, make_key("p", profile.name), profile.password), TAG, "save ssh pass");
    return save_index(KEY_SSH_INDEX, index);
}

esp_err_t SettingsStore::delete_ssh_profile(const std::string& name)
{
    auto index = load_index(KEY_SSH_INDEX, kMaxSshProfiles);
    index.erase(std::remove(index.begin(), index.end(), name), index.end());
    erase_namespace_key(NS_SSH, make_key("h", name));
    erase_namespace_key(NS_SSH, make_key("o", name));
    erase_namespace_key(NS_SSH, make_key("u", name));
    erase_namespace_key(NS_SSH, make_key("p", name));
    return save_index(KEY_SSH_INDEX, index);
}

std::vector<std::string> SettingsStore::load_index(const char* key, int max_count)
{
    auto values = split_index(get_string("indexes", key));
    if (static_cast<int>(values.size()) > max_count) {
        values.resize(max_count);
    }
    return values;
}

esp_err_t SettingsStore::save_index(const char* key, const std::vector<std::string>& values)
{
    return set_string("indexes", key, join_index(values));
}

std::string SettingsStore::get_string(const char* ns, const std::string& key)
{
    nvs_handle_t handle;
    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) {
        return "";
    }
    size_t required = 0;
    esp_err_t err = nvs_get_str(handle, key.c_str(), nullptr, &required);
    if (err != ESP_OK || required == 0) {
        nvs_close(handle);
        return "";
    }
    std::string value(required, '\0');
    err = nvs_get_str(handle, key.c_str(), value.data(), &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        return "";
    }
    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

esp_err_t SettingsStore::set_string(const char* ns, const std::string& key, const std::string& value)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(ns, NVS_READWRITE, &handle), TAG, "open nvs ns");
    esp_err_t err = nvs_set_str(handle, key.c_str(), value.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t SettingsStore::erase_namespace_key(const char* ns, const std::string& key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(handle, key.c_str());
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

}  // namespace adv
