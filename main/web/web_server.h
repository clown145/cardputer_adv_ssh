#pragma once

#include <string>

#include "esp_err.h"
#include "esp_http_server.h"
#include "storage/settings_store.h"

namespace adv {

class WebServer {
public:
    explicit WebServer(SettingsStore& store);
    ~WebServer();

    esp_err_t start();
    void stop();
    bool running() const;
    std::string password() const;

private:
    static esp_err_t root_handler(httpd_req_t* req);
    static esp_err_t login_handler(httpd_req_t* req);
    static esp_err_t state_handler(httpd_req_t* req);
    static esp_err_t save_profile_handler(httpd_req_t* req);
    static esp_err_t delete_profile_handler(httpd_req_t* req);
    static esp_err_t default_profile_handler(httpd_req_t* req);
    static esp_err_t settings_handler(httpd_req_t* req);
    static esp_err_t generate_key_handler(httpd_req_t* req);

    esp_err_t handle_root(httpd_req_t* req);
    esp_err_t handle_login(httpd_req_t* req);
    esp_err_t handle_state(httpd_req_t* req);
    esp_err_t handle_save_profile(httpd_req_t* req);
    esp_err_t handle_delete_profile(httpd_req_t* req);
    esp_err_t handle_default_profile(httpd_req_t* req);
    esp_err_t handle_settings(httpd_req_t* req);
    esp_err_t handle_generate_key(httpd_req_t* req);

    esp_err_t register_uri(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*));
    bool authorized(httpd_req_t* req) const;
    std::string read_body(httpd_req_t* req) const;
    esp_err_t send_json(httpd_req_t* req, const std::string& body, int status = 200) const;
    esp_err_t send_error(httpd_req_t* req, int status, const std::string& message) const;
    std::string state_json() const;

    SettingsStore& store_;
    httpd_handle_t server_ = nullptr;
    std::string password_;
};

}  // namespace adv
