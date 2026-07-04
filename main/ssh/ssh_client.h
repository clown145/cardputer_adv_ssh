#pragma once

#include <cstdint>
#include <string>

#include "esp_err.h"
#include "libssh2.h"
#include "storage/settings_store.h"

namespace adv {

enum class SshState {
    kIdle,
    kResolving,
    kConnecting,
    kAuthenticating,
    kConnected,
    kFailed,
};

struct SshExecResult {
    int exit_code = -1;
    std::string output;
};

class SshClient {
public:
    SshClient();
    ~SshClient();

    esp_err_t connect_password(const SshProfile& profile);
    esp_err_t exec(const std::string& command, SshExecResult& result);
    esp_err_t open_shell(uint16_t cols = 80, uint16_t rows = 24);
    esp_err_t write_shell(const std::string& data);
    esp_err_t read_shell(std::string& output, uint32_t quiet_ms);
    esp_err_t send_shell_line(const std::string& line, std::string& output);
    void close_shell();
    void disconnect();

    bool connected() const;
    SshState state() const;
    std::string last_error() const;

private:
    void set_error(const std::string& message);

    int socket_fd_ = -1;
    LIBSSH2_SESSION* session_ = nullptr;
    LIBSSH2_CHANNEL* shell_channel_ = nullptr;
    uint16_t shell_cols_ = 0;
    uint16_t shell_rows_ = 0;
    SshState state_ = SshState::kIdle;
    std::string last_error_;
};

}  // namespace adv
