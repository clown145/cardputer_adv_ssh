#include "ssh/ssh_client.h"

#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

namespace adv {
namespace {
constexpr const char* TAG = "ssh";
constexpr int kReadBufferSize = 512;
constexpr size_t kMaxReadDrainBytes = 4096;
constexpr int kShellDrainMs = 700;

std::string libssh2_error(LIBSSH2_SESSION* session, const char* fallback)
{
    if (session == nullptr) {
        return fallback;
    }
    char* message = nullptr;
    int length = 0;
    int code = libssh2_session_last_error(session, &message, &length, 0);
    if (code == 0 || message == nullptr || length <= 0) {
        return fallback;
    }
    return std::string(message, length);
}
}  // namespace

SshClient::SshClient()
{
    libssh2_init(0);
}

SshClient::~SshClient()
{
    disconnect();
    libssh2_exit();
}

esp_err_t SshClient::connect_password(const SshProfile& profile)
{
    disconnect();
    last_error_.clear();
    state_ = SshState::kResolving;

    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    std::string port = std::to_string(profile.port);
    int rc = getaddrinfo(profile.host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        set_error("dns lookup failed");
        return ESP_FAIL;
    }

    state_ = SshState::kConnecting;
    for (addrinfo* item = result; item != nullptr; item = item->ai_next) {
        socket_fd_ = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (socket_fd_ < 0) {
            continue;
        }
        rc = ::connect(socket_fd_, item->ai_addr, item->ai_addrlen);
        if (rc == 0) {
            break;
        }
        close(socket_fd_);
        socket_fd_ = -1;
    }
    freeaddrinfo(result);
    if (socket_fd_ < 0) {
        set_error(std::string("tcp connect failed: ") + std::strerror(errno));
        return ESP_FAIL;
    }

    session_ = libssh2_session_init();
    if (session_ == nullptr) {
        set_error("ssh session init failed");
        disconnect();
        return ESP_FAIL;
    }

    libssh2_session_set_blocking(session_, 1);
    rc = libssh2_session_handshake(session_, socket_fd_);
    if (rc != 0) {
        set_error(libssh2_error(session_, "ssh handshake failed"));
        disconnect();
        return ESP_FAIL;
    }

    state_ = SshState::kAuthenticating;
    rc = libssh2_userauth_password(session_, profile.username.c_str(), profile.password.c_str());
    if (rc != 0) {
        set_error(libssh2_error(session_, "ssh password auth failed"));
        disconnect();
        return ESP_FAIL;
    }

    state_ = SshState::kConnected;
    ESP_LOGI(TAG, "connected to %s:%u as %s", profile.host.c_str(), profile.port, profile.username.c_str());
    return ESP_OK;
}

esp_err_t SshClient::exec(const std::string& command, SshExecResult& result)
{
    result = {};
    if (session_ == nullptr || state_ != SshState::kConnected) {
        set_error("ssh is not connected");
        return ESP_ERR_INVALID_STATE;
    }

    LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(session_);
    if (channel == nullptr) {
        set_error(libssh2_error(session_, "ssh channel open failed"));
        return ESP_FAIL;
    }

    int rc = libssh2_channel_exec(channel, command.c_str());
    if (rc != 0) {
        set_error(libssh2_error(session_, "ssh exec failed"));
        libssh2_channel_free(channel);
        return ESP_FAIL;
    }

    char buffer[kReadBufferSize];
    while (true) {
        ssize_t read = libssh2_channel_read(channel, buffer, sizeof(buffer));
        if (read > 0) {
            result.output.append(buffer, buffer + read);
            continue;
        }
        if (read == LIBSSH2_ERROR_EAGAIN) {
            continue;
        }
        break;
    }

    while (!libssh2_channel_eof(channel)) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    result.exit_code = libssh2_channel_get_exit_status(channel);
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    return ESP_OK;
}

esp_err_t SshClient::open_shell(uint16_t cols, uint16_t rows)
{
    if (session_ == nullptr || state_ != SshState::kConnected) {
        set_error("ssh is not connected");
        return ESP_ERR_INVALID_STATE;
    }
    if (shell_channel_ != nullptr) {
        if (shell_cols_ == cols && shell_rows_ == rows) {
            return ESP_OK;
        }
        close_shell();
    }

    shell_channel_ = libssh2_channel_open_session(session_);
    if (shell_channel_ == nullptr) {
        set_error(libssh2_error(session_, "ssh shell channel open failed"));
        return ESP_FAIL;
    }

    auto request_env = [&](const char* name, const char* value) {
        int env_rc = libssh2_channel_setenv(shell_channel_, name, value);
        if (env_rc != 0) {
            ESP_LOGW(TAG, "ssh server rejected env %s=%s", name, value);
        }
    };
    request_env("LANG", "C.UTF-8");
    request_env("LC_CTYPE", "C.UTF-8");

    int rc = libssh2_channel_request_pty_ex(shell_channel_, "xterm", 5, nullptr, 0, cols, rows, 0, 0);
    if (rc != 0) {
        set_error(libssh2_error(session_, "ssh pty request failed"));
        close_shell();
        return ESP_FAIL;
    }

    rc = libssh2_channel_shell(shell_channel_);
    if (rc != 0) {
        set_error(libssh2_error(session_, "ssh shell start failed"));
        close_shell();
        return ESP_FAIL;
    }
    libssh2_channel_set_blocking(shell_channel_, 0);
    shell_cols_ = cols;
    shell_rows_ = rows;
    return ESP_OK;
}

esp_err_t SshClient::resize_shell(uint16_t cols, uint16_t rows)
{
    if (shell_channel_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (cols == shell_cols_ && rows == shell_rows_) {
        return ESP_OK;
    }

    int rc = libssh2_channel_request_pty_size(shell_channel_, cols, rows);
    if (rc == LIBSSH2_ERROR_EAGAIN) {
        vTaskDelay(pdMS_TO_TICKS(20));
        rc = libssh2_channel_request_pty_size(shell_channel_, cols, rows);
    }
    if (rc != 0) {
        last_error_ = libssh2_error(session_, "ssh pty resize failed");
        ESP_LOGW(TAG, "%s", last_error_.c_str());
        return ESP_FAIL;
    }
    shell_cols_ = cols;
    shell_rows_ = rows;
    return ESP_OK;
}

esp_err_t SshClient::send_shell_line(const std::string& line, std::string& output)
{
    output.clear();
    std::string payload = line + "\n";
    ESP_RETURN_ON_ERROR(write_shell(payload), TAG, "write shell line");
    return read_shell(output, kShellDrainMs);
}

esp_err_t SshClient::write_shell(const std::string& data)
{
    if (shell_channel_ == nullptr) {
        ESP_RETURN_ON_ERROR(open_shell(), TAG, "open shell");
    }
    size_t written = 0;
    while (written < data.size()) {
        ssize_t rc = libssh2_channel_write(shell_channel_, data.data() + written, data.size() - written);
        if (rc > 0) {
            written += rc;
            continue;
        }
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        set_error(libssh2_error(session_, "ssh shell write failed"));
        close_shell();
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t SshClient::read_shell(std::string& output, uint32_t quiet_ms)
{
    output.clear();
    if (shell_channel_ == nullptr) {
        ESP_RETURN_ON_ERROR(open_shell(), TAG, "open shell");
    }

    char buffer[kReadBufferSize];
    uint32_t waited_ms = 0;
    while (true) {
        ssize_t rc = libssh2_channel_read(shell_channel_, buffer, sizeof(buffer));
        if (rc > 0) {
            output.append(buffer, buffer + rc);
            waited_ms = 0;
            if (quiet_ms == 0 && output.size() >= kMaxReadDrainBytes) {
                break;
            }
            continue;
        }
        if (rc == LIBSSH2_ERROR_EAGAIN || rc == 0) {
            if (quiet_ms == 0 || waited_ms >= quiet_ms) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            waited_ms += 50;
            continue;
        }
        set_error(libssh2_error(session_, "ssh shell read failed"));
        close_shell();
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t SshClient::read_shell_chunk(std::string& output, size_t max_bytes)
{
    output.clear();
    if (shell_channel_ == nullptr) {
        ESP_RETURN_ON_ERROR(open_shell(), TAG, "open shell");
    }
    if (max_bytes == 0) {
        return ESP_OK;
    }

    char buffer[kReadBufferSize];
    while (output.size() < max_bytes) {
        size_t wanted = std::min(sizeof(buffer), max_bytes - output.size());
        ssize_t rc = libssh2_channel_read(shell_channel_, buffer, wanted);
        if (rc > 0) {
            output.append(buffer, buffer + rc);
            continue;
        }
        if (rc == LIBSSH2_ERROR_EAGAIN || rc == 0) {
            break;
        }
        set_error(libssh2_error(session_, "ssh shell read failed"));
        close_shell();
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t SshClient::send_signal(const char* signal)
{
    if (shell_channel_ == nullptr) {
        ESP_RETURN_ON_ERROR(open_shell(), TAG, "open shell");
    }
    int rc = libssh2_channel_signal(shell_channel_, signal);
    if (rc == LIBSSH2_ERROR_EAGAIN) {
        vTaskDelay(pdMS_TO_TICKS(20));
        rc = libssh2_channel_signal(shell_channel_, signal);
    }
    if (rc != 0) {
        last_error_ = libssh2_error(session_, "ssh signal failed");
        ESP_LOGW(TAG, "%s", last_error_.c_str());
        return ESP_FAIL;
    }
    return ESP_OK;
}

void SshClient::close_shell()
{
    if (shell_channel_ != nullptr) {
        libssh2_channel_close(shell_channel_);
        libssh2_channel_free(shell_channel_);
        shell_channel_ = nullptr;
        shell_cols_ = 0;
        shell_rows_ = 0;
    }
}

void SshClient::disconnect()
{
    close_shell();
    if (session_ != nullptr) {
        libssh2_session_disconnect(session_, "client disconnect");
        libssh2_session_free(session_);
        session_ = nullptr;
    }
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    state_ = SshState::kIdle;
}

bool SshClient::connected() const
{
    return state_ == SshState::kConnected && session_ != nullptr;
}

SshState SshClient::state() const
{
    return state_;
}

std::string SshClient::last_error() const
{
    return last_error_;
}

void SshClient::set_error(const std::string& message)
{
    last_error_ = message;
    state_ = SshState::kFailed;
    ESP_LOGE(TAG, "%s", message.c_str());
}

}  // namespace adv
