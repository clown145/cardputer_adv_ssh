#include "web/web_server.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"

namespace adv {
namespace {
constexpr const char* TAG = "web";
constexpr size_t kMaxBodyBytes = 4096;

constexpr const char* kIndexHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cardputer-Adv</title>
<style>
:root{color-scheme:dark;--bg:#0d1117;--panel:#161b22;--line:#30363d;--text:#f0f6fc;--muted:#8b949e;--accent:#2ea043;--warn:#d29922;--bad:#f85149}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.4 system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
header{display:flex;align-items:center;justify-content:space-between;padding:14px 16px;border-bottom:1px solid var(--line);background:#010409}
main{max-width:920px;margin:0 auto;padding:16px;display:grid;gap:12px}
h1{margin:0;font-size:18px;font-weight:650}h2{margin:0 0 10px;font-size:14px;color:var(--muted);font-weight:650;text-transform:uppercase}
.pill{border:1px solid var(--line);border-radius:999px;padding:4px 9px;color:var(--muted);font-size:12px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.panel{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:14px}
label{display:block;color:var(--muted);font-size:12px;margin:10px 0 5px}
input,select,textarea{width:100%;border:1px solid var(--line);border-radius:6px;background:#0d1117;color:var(--text);padding:9px;font:inherit}
textarea{min-height:110px;resize:vertical;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px}
button{border:1px solid var(--line);border-radius:6px;background:#21262d;color:var(--text);padding:9px 11px;font:inherit;cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:white}button.warn{border-color:var(--warn);color:#ffd58a}button.bad{border-color:var(--bad);color:#ffaba8}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.row>*{flex:1}.row button{flex:0 0 auto}
.list{display:grid;gap:7px}.item{display:flex;justify-content:space-between;gap:8px;padding:8px;border:1px solid var(--line);border-radius:6px;background:#0d1117}
.muted{color:var(--muted)}.status{min-height:20px;color:var(--muted)}.hide{display:none}.login{max-width:360px;margin:12vh auto}
@media(max-width:720px){.grid{grid-template-columns:1fr}.row{display:grid}.row button{width:100%}}
</style>
</head>
<body>
<header><h1>Cardputer-Adv</h1><span id="badge" class="pill">WebUI</span></header>
<main>
<section id="login" class="panel login">
<h2>Login</h2>
<label for="pass">Device password</label>
<input id="pass" type="password" autocomplete="current-password">
<div class="row" style="margin-top:12px"><button class="primary" onclick="login()">Unlock</button></div>
<div id="loginStatus" class="status"></div>
</section>
<section id="app" class="hide">
<div class="grid">
<section class="panel">
<h2>SSH Profiles</h2>
<div id="profiles" class="list"></div>
<label for="profilePick">Edit</label>
<select id="profilePick" onchange="pickProfile()"></select>
<label for="server">Server</label>
<input id="server" placeholder="user@host:22" autocomplete="off">
<label for="sshPass">Password</label>
<input id="sshPass" type="password" autocomplete="new-password">
<div class="row" style="margin-top:12px">
<button class="primary" onclick="saveProfile()">Save</button>
<button onclick="setDefault()">Set default</button>
<button class="bad" onclick="deleteProfile()">Delete</button>
</div>
</section>
<section class="panel">
<h2>Device Key</h2>
<textarea id="publicKey" readonly spellcheck="false"></textarea>
<div class="row" style="margin-top:12px">
<button onclick="copyKey()">Copy</button>
<button class="warn" onclick="generateKey()">Generate</button>
</div>
</section>
</div>
<div class="grid">
<section class="panel">
<h2>Terminal</h2>
<label for="chrome">Chrome mode</label>
<select id="chrome"><option value="full">Full</option><option value="compact">Compact</option><option value="hidden">Hidden</option></select>
<label for="theme">Theme</label>
<select id="theme"><option value="adv_dark">Adv Dark</option><option value="true_black">True Black</option><option value="solarized_dark">Solarized Dark</option><option value="gruvbox_dark">Gruvbox Dark</option><option value="dracula">Dracula</option><option value="nord">Nord</option><option value="tokyo_night">Tokyo Night</option><option value="catppuccin_mocha">Catppuccin Mocha</option><option value="monokai">Monokai</option></select>
<div class="row" style="margin-top:12px"><button class="primary" onclick="saveSettings()">Save</button></div>
</section>
<section class="panel">
<h2>Status</h2>
<div id="status" class="status"></div>
</section>
</div>
</section>
</main>
<script>
let token=localStorage.getItem("advToken")||"";
let state=null;
const $=id=>document.getElementById(id);
function setStatus(msg){$("status").textContent=msg||""}
function api(path,opts={}){
  const sep=path.indexOf("?")>=0?"&":"?";
  opts.headers=Object.assign({"Content-Type":"application/json","X-Adv-Password":token},opts.headers||{});
  return fetch(path+sep+"token="+encodeURIComponent(token),opts).then(async r=>{
    const text=await r.text(); let data={}; try{data=text?JSON.parse(text):{};}catch(e){}
    if(!r.ok) throw new Error(data.error||r.statusText);
    return data;
  });
}
function login(){
  token=$("pass").value.trim();
  fetch("/api/login",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({password:token})})
    .then(async r=>{const data=await r.json();if(!r.ok||!data.ok)throw new Error(data.error||"Login failed");localStorage.setItem("advToken",token);load();})
    .catch(e=>$("loginStatus").textContent=e.message);
}
function load(){
  api("/api/state").then(data=>{state=data;$("login").classList.add("hide");$("app").classList.remove("hide");render();})
    .catch(()=>{$("login").classList.remove("hide");$("app").classList.add("hide");});
}
function serverText(p){return p.server||`${p.username}@${p.host}${p.port&&p.port!==22?":"+p.port:""}`}
function render(){
  $("badge").textContent=state.defaultProfile?`Default: ${state.defaultProfile}`:"WebUI";
  $("chrome").value=state.terminalChrome||"full";
  $("theme").value=state.terminalTheme||"adv_dark";
  $("publicKey").value=state.publicKey||"";
  $("profiles").innerHTML=(state.profiles||[]).map(p=>`<div class="item"><span>${escapeHtml(serverText(p))}</span><span class="muted">${p.name===state.defaultProfile?"default":""}</span></div>`).join("")||"<div class=\"muted\">No profiles</div>";
  $("profilePick").innerHTML="<option value=''>New profile</option>"+(state.profiles||[]).map(p=>`<option value="${escapeHtml(p.name)}">${escapeHtml(serverText(p))}</option>`).join("");
  pickProfile();
  setStatus(state.hasPrivateKey?"Key ready":"No device key");
}
function escapeHtml(v){return String(v||"").replaceAll("&","&amp;").replaceAll("\"","&quot;").replaceAll("<","&lt;").replaceAll(">","&gt;").replaceAll("'","&#39;")}
function currentProfile(){return (state.profiles||[]).find(p=>p.name===$("profilePick").value)}
function pickProfile(){
  const p=currentProfile();
  $("server").value=p?serverText(p):"";
  $("sshPass").value=p?(p.password||""):"";
}
function saveProfile(){
  api("/api/profile",{method:"POST",body:JSON.stringify({previous:$("profilePick").value,server:$("server").value,password:$("sshPass").value})})
    .then(load).catch(e=>setStatus(e.message));
}
function deleteProfile(){
  const name=$("profilePick").value;if(!name)return;
  api("/api/profile/delete",{method:"POST",body:JSON.stringify({name})}).then(load).catch(e=>setStatus(e.message));
}
function setDefault(){
  const name=$("profilePick").value;if(!name)return;
  api("/api/profile/default",{method:"POST",body:JSON.stringify({name})}).then(load).catch(e=>setStatus(e.message));
}
function saveSettings(){
  api("/api/settings",{method:"POST",body:JSON.stringify({terminalChrome:$("chrome").value,terminalTheme:$("theme").value})}).then(load).catch(e=>setStatus(e.message));
}
function generateKey(){
  setStatus("Generating key...");
  api("/api/key/generate",{method:"POST",body:"{}"}).then(load).catch(e=>setStatus(e.message));
}
function copyKey(){navigator.clipboard.writeText($("publicKey").value).then(()=>setStatus("Copied"))}
$("pass").addEventListener("keydown",e=>{if(e.key==="Enter")login()});
if(token)load();
</script>
</body>
</html>)HTML";

std::string server_string(const SshProfile& profile)
{
    std::string host = profile.host;
    if (host.find(':') != std::string::npos && !(host.size() > 1 && host.front() == '[' && host.back() == ']')) {
        host = "[" + host + "]";
    }
    std::string out = profile.username + "@" + host;
    if (profile.port != 22 && profile.port != 0) {
        out += ":" + std::to_string(profile.port);
    }
    return out;
}

bool parse_server_string(const std::string& raw, SshProfile& profile)
{
    auto at = raw.find('@');
    if (at == std::string::npos || at == 0 || at == raw.size() - 1) {
        return false;
    }

    profile.username = raw.substr(0, at);
    std::string host_port = raw.substr(at + 1);
    profile.port = 22;

    if (!host_port.empty() && host_port.front() == '[') {
        auto close = host_port.find(']');
        if (close == std::string::npos || close == 1) {
            return false;
        }
        profile.host = host_port.substr(1, close - 1);
        if (close + 1 < host_port.size()) {
            if (host_port[close + 1] != ':') {
                return false;
            }
            profile.port = static_cast<uint16_t>(std::atoi(host_port.substr(close + 2).c_str()));
        }
    } else {
        size_t first_colon = host_port.find(':');
        size_t last_colon = host_port.rfind(':');
        if (first_colon != std::string::npos && first_colon == last_colon) {
            profile.host = host_port.substr(0, first_colon);
            profile.port = static_cast<uint16_t>(std::atoi(host_port.substr(first_colon + 1).c_str()));
        } else {
            profile.host = host_port;
        }
    }

    if (profile.host.empty()) {
        return false;
    }
    if (profile.port == 0) {
        profile.port = 22;
    }
    profile.name = raw;
    return true;
}

const char* status_text(int status)
{
    switch (status) {
        case 200:
            return "200 OK";
        case 400:
            return "400 Bad Request";
        case 401:
            return "401 Unauthorized";
        case 413:
            return "413 Payload Too Large";
        case 500:
        default:
            return "500 Internal Server Error";
    }
}

std::string json_string(const cJSON* root, const char* key)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

bool valid_terminal_theme(const std::string& theme)
{
    static constexpr const char* kThemes[] = {
        "adv_dark",       "true_black", "solarized_dark",    "gruvbox_dark", "dracula",
        "nord",           "tokyo_night", "catppuccin_mocha", "monokai",
    };
    for (const char* item : kThemes) {
        if (theme == item) {
            return true;
        }
    }
    return false;
}

void append_uint32(std::vector<unsigned char>& out, uint32_t value)
{
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>(value & 0xff));
}

void append_string(std::vector<unsigned char>& out, const unsigned char* data, size_t len)
{
    append_uint32(out, static_cast<uint32_t>(len));
    out.insert(out.end(), data, data + len);
}

bool append_mpint(std::vector<unsigned char>& out, const mbedtls_mpi& value)
{
    size_t len = mbedtls_mpi_size(&value);
    if (len == 0) {
        append_uint32(out, 0);
        return true;
    }
    std::vector<unsigned char> data(len);
    if (mbedtls_mpi_write_binary(&value, data.data(), data.size()) != 0) {
        return false;
    }
    if ((data[0] & 0x80) != 0) {
        append_uint32(out, static_cast<uint32_t>(data.size() + 1));
        out.push_back(0);
        out.insert(out.end(), data.begin(), data.end());
    } else {
        append_string(out, data.data(), data.size());
    }
    return true;
}

bool base64_encode(const std::vector<unsigned char>& data, std::string& out)
{
    size_t olen = 0;
    std::vector<unsigned char> encoded(((data.size() + 2) / 3) * 4 + 1);
    if (mbedtls_base64_encode(encoded.data(), encoded.size(), &olen, data.data(), data.size()) != 0) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(encoded.data()), olen);
    return true;
}

esp_err_t make_openssh_public_key(const mbedtls_pk_context& pk, std::string& public_key)
{
    mbedtls_rsa_context* rsa = mbedtls_pk_rsa(pk);
    if (rsa == nullptr) {
        return ESP_FAIL;
    }

    mbedtls_mpi n;
    mbedtls_mpi e;
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&e);
    int rc = mbedtls_rsa_export(rsa, &n, nullptr, nullptr, nullptr, &e);
    if (rc != 0) {
        mbedtls_mpi_free(&n);
        mbedtls_mpi_free(&e);
        return ESP_FAIL;
    }

    std::vector<unsigned char> wire;
    constexpr const char* type = "ssh-rsa";
    append_string(wire, reinterpret_cast<const unsigned char*>(type), std::strlen(type));
    bool ok = append_mpint(wire, e) && append_mpint(wire, n);
    mbedtls_mpi_free(&n);
    mbedtls_mpi_free(&e);
    if (!ok) {
        return ESP_FAIL;
    }

    std::string encoded;
    if (!base64_encode(wire, encoded)) {
        return ESP_FAIL;
    }
    public_key = "ssh-rsa " + encoded + " cardputer-adv";
    return ESP_OK;
}

esp_err_t generate_ssh_key_pair(std::string& private_key, std::string& public_key)
{
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char* personal = "cardputer-adv-ssh";
    int rc = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                   reinterpret_cast<const unsigned char*>(personal), std::strlen(personal));
    if (rc == 0) {
        rc = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    }
    if (rc == 0) {
        rc = mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "key generation failed: -0x%04x", -rc);
        mbedtls_pk_free(&pk);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return ESP_FAIL;
    }

    std::vector<unsigned char> pem(4096);
    rc = mbedtls_pk_write_key_pem(&pk, pem.data(), pem.size());
    if (rc != 0 || make_openssh_public_key(pk, public_key) != ESP_OK) {
        ESP_LOGE(TAG, "key export failed: -0x%04x", -rc);
        mbedtls_pk_free(&pk);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return ESP_FAIL;
    }

    private_key.assign(reinterpret_cast<const char*>(pem.data()));
    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ESP_OK;
}
}  // namespace

WebServer::WebServer(SettingsStore& store) : store_(store) {}

WebServer::~WebServer()
{
    stop();
}

esp_err_t WebServer::start()
{
    if (server_ != nullptr) {
        return ESP_OK;
    }

    char password[8] = {};
    std::snprintf(password, sizeof(password), "%06lu", static_cast<unsigned long>(esp_random() % 1000000));
    password_ = password;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 12288;
    config.max_open_sockets = 3;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        server_ = nullptr;
        password_.clear();
        return err;
    }

    auto register_or_stop = [&](const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*)) {
        esp_err_t uri_err = register_uri(uri, method, handler);
        if (uri_err != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", uri, esp_err_to_name(uri_err));
            stop();
        }
        return uri_err;
    };

    ESP_RETURN_ON_ERROR(register_or_stop("/", HTTP_GET, &WebServer::root_handler), TAG, "register root");
    ESP_RETURN_ON_ERROR(register_or_stop("/api/login", HTTP_POST, &WebServer::login_handler), TAG, "register login");
    ESP_RETURN_ON_ERROR(register_or_stop("/api/state", HTTP_GET, &WebServer::state_handler), TAG, "register state");
    ESP_RETURN_ON_ERROR(register_or_stop("/api/profile", HTTP_POST, &WebServer::save_profile_handler), TAG,
                        "register profile");
    ESP_RETURN_ON_ERROR(register_or_stop("/api/profile/delete", HTTP_POST, &WebServer::delete_profile_handler), TAG,
                        "register delete");
    ESP_RETURN_ON_ERROR(register_or_stop("/api/profile/default", HTTP_POST, &WebServer::default_profile_handler), TAG,
                        "register default");
    ESP_RETURN_ON_ERROR(register_or_stop("/api/settings", HTTP_POST, &WebServer::settings_handler), TAG,
                        "register settings");
    ESP_RETURN_ON_ERROR(register_or_stop("/api/key/generate", HTTP_POST, &WebServer::generate_key_handler), TAG,
                        "register key");
    ESP_LOGI(TAG, "webui started");
    return ESP_OK;
}

void WebServer::stop()
{
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    password_.clear();
}

bool WebServer::running() const
{
    return server_ != nullptr;
}

std::string WebServer::password() const
{
    return password_;
}

esp_err_t WebServer::register_uri(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*))
{
    httpd_uri_t item = {};
    item.uri = uri;
    item.method = method;
    item.handler = handler;
    item.user_ctx = this;
    return httpd_register_uri_handler(server_, &item);
}

bool WebServer::authorized(httpd_req_t* req) const
{
    if (password_.empty()) {
        return false;
    }

    char header[32] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Adv-Password", header, sizeof(header)) == ESP_OK &&
        password_ == header) {
        return true;
    }

    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0 || query_len >= 96) {
        return false;
    }
    std::string query(query_len + 1, '\0');
    if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) {
        return false;
    }
    char token[32] = {};
    if (httpd_query_key_value(query.c_str(), "token", token, sizeof(token)) != ESP_OK) {
        return false;
    }
    return password_ == token;
}

std::string WebServer::read_body(httpd_req_t* req) const
{
    if (req->content_len > kMaxBodyBytes) {
        return "";
    }
    std::string body(req->content_len, '\0');
    size_t received = 0;
    while (received < body.size()) {
        int rc = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (rc == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (rc <= 0) {
            return "";
        }
        received += static_cast<size_t>(rc);
    }
    return body;
}

esp_err_t WebServer::send_json(httpd_req_t* req, const std::string& body, int status) const
{
    httpd_resp_set_status(req, status_text(status));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t WebServer::send_error(httpd_req_t* req, int status, const std::string& message) const
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message.c_str());
    char* raw = cJSON_PrintUnformatted(root);
    std::string body = raw == nullptr ? "{\"error\":\"failed\"}" : raw;
    if (raw != nullptr) {
        cJSON_free(raw);
    }
    cJSON_Delete(root);
    return send_json(req, body, status);
}

std::string WebServer::state_json() const
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "defaultProfile", store_.load_default_ssh_profile().c_str());
    std::string chrome = store_.load_terminal_chrome_mode();
    cJSON_AddStringToObject(root, "terminalChrome", chrome.empty() ? "full" : chrome.c_str());
    std::string theme = store_.load_terminal_theme();
    cJSON_AddStringToObject(root, "terminalTheme", theme.empty() ? "adv_dark" : theme.c_str());
    std::string public_key = store_.load_ssh_public_key();
    cJSON_AddStringToObject(root, "publicKey", public_key.c_str());
    cJSON_AddBoolToObject(root, "hasPrivateKey", !store_.load_ssh_private_key().empty());

    cJSON* profiles = cJSON_AddArrayToObject(root, "profiles");
    for (const auto& profile : store_.load_ssh_profiles()) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", profile.name.c_str());
        cJSON_AddStringToObject(item, "server", server_string(profile).c_str());
        cJSON_AddStringToObject(item, "host", profile.host.c_str());
        cJSON_AddNumberToObject(item, "port", profile.port);
        cJSON_AddStringToObject(item, "username", profile.username.c_str());
        cJSON_AddStringToObject(item, "password", profile.password.c_str());
        cJSON_AddItemToArray(profiles, item);
    }

    char* raw = cJSON_PrintUnformatted(root);
    std::string body = raw == nullptr ? "{}" : raw;
    if (raw != nullptr) {
        cJSON_free(raw);
    }
    cJSON_Delete(root);
    return body;
}

esp_err_t WebServer::root_handler(httpd_req_t* req)
{
    return static_cast<WebServer*>(req->user_ctx)->handle_root(req);
}

esp_err_t WebServer::login_handler(httpd_req_t* req)
{
    return static_cast<WebServer*>(req->user_ctx)->handle_login(req);
}

esp_err_t WebServer::state_handler(httpd_req_t* req)
{
    return static_cast<WebServer*>(req->user_ctx)->handle_state(req);
}

esp_err_t WebServer::save_profile_handler(httpd_req_t* req)
{
    return static_cast<WebServer*>(req->user_ctx)->handle_save_profile(req);
}

esp_err_t WebServer::delete_profile_handler(httpd_req_t* req)
{
    return static_cast<WebServer*>(req->user_ctx)->handle_delete_profile(req);
}

esp_err_t WebServer::default_profile_handler(httpd_req_t* req)
{
    return static_cast<WebServer*>(req->user_ctx)->handle_default_profile(req);
}

esp_err_t WebServer::settings_handler(httpd_req_t* req)
{
    return static_cast<WebServer*>(req->user_ctx)->handle_settings(req);
}

esp_err_t WebServer::generate_key_handler(httpd_req_t* req)
{
    return static_cast<WebServer*>(req->user_ctx)->handle_generate_key(req);
}

esp_err_t WebServer::handle_root(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, kIndexHtml);
}

esp_err_t WebServer::handle_login(httpd_req_t* req)
{
    std::string body = read_body(req);
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    std::string password = root == nullptr ? "" : json_string(root, "password");
    if (root != nullptr) {
        cJSON_Delete(root);
    }
    if (password != password_) {
        return send_error(req, 401, "Invalid password");
    }
    return send_json(req, "{\"ok\":true}");
}

esp_err_t WebServer::handle_state(httpd_req_t* req)
{
    if (!authorized(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    return send_json(req, state_json());
}

esp_err_t WebServer::handle_save_profile(httpd_req_t* req)
{
    if (!authorized(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    if (req->content_len > kMaxBodyBytes) {
        return send_error(req, 413, "Request is too large");
    }

    std::string body = read_body(req);
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr) {
        return send_error(req, 400, "Invalid JSON");
    }

    std::string previous = json_string(root, "previous");
    std::string server = json_string(root, "server");
    std::string password = json_string(root, "password");
    cJSON_Delete(root);

    SshProfile profile;
    if (server.empty() || !parse_server_string(server, profile)) {
        return send_error(req, 400, "Invalid server");
    }
    profile.password = password;

    std::string default_name = store_.load_default_ssh_profile();
    if (!previous.empty() && previous != profile.name) {
        store_.delete_ssh_profile(previous);
        if (default_name == previous) {
            store_.save_default_ssh_profile(profile.name);
        }
    }

    esp_err_t err = store_.save_ssh_profile(profile);
    if (err != ESP_OK) {
        return send_error(req, 500, "Save failed");
    }
    return send_json(req, state_json());
}

esp_err_t WebServer::handle_delete_profile(httpd_req_t* req)
{
    if (!authorized(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    std::string body = read_body(req);
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr) {
        return send_error(req, 400, "Invalid JSON");
    }
    std::string name = json_string(root, "name");
    cJSON_Delete(root);
    if (name.empty()) {
        return send_error(req, 400, "Missing profile");
    }
    store_.delete_ssh_profile(name);
    return send_json(req, state_json());
}

esp_err_t WebServer::handle_default_profile(httpd_req_t* req)
{
    if (!authorized(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    std::string body = read_body(req);
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr) {
        return send_error(req, 400, "Invalid JSON");
    }
    std::string name = json_string(root, "name");
    cJSON_Delete(root);
    store_.save_default_ssh_profile(name);
    return send_json(req, state_json());
}

esp_err_t WebServer::handle_settings(httpd_req_t* req)
{
    if (!authorized(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    std::string body = read_body(req);
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr) {
        return send_error(req, 400, "Invalid JSON");
    }
    std::string chrome = json_string(root, "terminalChrome");
    std::string theme = json_string(root, "terminalTheme");
    cJSON_Delete(root);
    if (chrome != "full" && chrome != "compact" && chrome != "hidden") {
        return send_error(req, 400, "Invalid terminal mode");
    }
    if (theme.empty()) {
        theme = "adv_dark";
    }
    if (!valid_terminal_theme(theme)) {
        return send_error(req, 400, "Invalid terminal theme");
    }
    store_.save_terminal_chrome_mode(chrome);
    store_.save_terminal_theme(theme);
    return send_json(req, state_json());
}

esp_err_t WebServer::handle_generate_key(httpd_req_t* req)
{
    if (!authorized(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    if (req->content_len > kMaxBodyBytes) {
        return send_error(req, 413, "Request is too large");
    }
    (void)read_body(req);

    std::string private_key;
    std::string public_key;
    if (generate_ssh_key_pair(private_key, public_key) != ESP_OK) {
        return send_error(req, 500, "Key generation failed");
    }
    if (store_.save_ssh_key_pair(private_key, public_key) != ESP_OK) {
        return send_error(req, 500, "Key save failed");
    }
    return send_json(req, state_json());
}

}  // namespace adv
