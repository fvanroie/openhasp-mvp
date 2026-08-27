#include "hasp_http.hpp"
#include "esp_log.h"
#include "esp_random.h"

#include <ArduinoJson.h>

static const char *TAG = "HASP_HTTP";

#define REQUIRE_AUTH(req)                                      \
    do                                                         \
    {                                                          \
        auto *self = from_req(req); \
        if (self->require_auth(req) != ESP_OK)                 \
            return ESP_FAIL;                                   \
    } while (0)

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------
static constexpr size_t MAX_SESSIONS = 3;
static constexpr uint32_t SESSION_TIMEOUT_MS = 30 * 60 * 1000;

Session sessions_[MAX_SESSIONS]{};

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

static bool constant_time_equal(
    const uint8_t *a,
    const uint8_t *b,
    size_t len)
{
    uint8_t diff = 0;

    for (size_t i = 0; i < len; ++i)
    {
        diff |= a[i] ^ b[i];
    }

    return diff == 0;
}

// -----------------------------------------------------------------------------
// Create session
// -----------------------------------------------------------------------------

esp_err_t HaspHttp::create_session(httpd_req_t *req)
{
    const uint32_t now = esp_log_timestamp();

    Session *slot = nullptr;

    // Find an unused or expired session slot.
    for (auto &session : sessions_)
    {
        if (!session.active ||
            (now - session.last_activity) > SESSION_TIMEOUT_MS)
        {
            // Clear any expired session before reusing it.
            session.active = false;
            slot = &session;
            break;
        }
    }

    if (!slot)
    {
        // All three session slots are currently active.
        return ESP_ERR_INVALID_STATE;
    }

    // Generate a fresh cryptographically-random session token.
    esp_fill_random(slot->token, sizeof(slot->token));

    slot->active = true;
    slot->last_activity = now;

    // Convert binary token to hexadecimal for the cookie.
    char token_hex[SESSION_TOKEN_LEN * 2 + 1];

    static constexpr char hex[] = "0123456789abcdef";

    for (size_t i = 0; i < SESSION_TOKEN_LEN; ++i)
    {
        token_hex[i * 2] = hex[(slot->token[i] >> 4) & 0x0f];
        token_hex[i * 2 + 1] = hex[slot->token[i] & 0x0f];
    }

    token_hex[SESSION_TOKEN_LEN * 2] = '\0';

    // 64 hex chars + cookie attributes.
    char cookie[128];

    snprintf(
        cookie,
        sizeof(cookie),
        "hasp_session=%s; HttpOnly; Secure; SameSite=Strict; Path=/",
        token_hex);

    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    httpd_resp_sendstr(req, "OK");

    return ESP_OK;
}

void HaspHttp::clear_session(Session &session)
{
    memset(session.token, 0, sizeof(session.token));
    session.active = false;
    session.last_activity = 0;
    // session.created_at = 0;
}

// -----------------------------------------------------------------------------
// Authenticate request
// -----------------------------------------------------------------------------

esp_err_t HaspHttp::authenticate_request(httpd_req_t *req)
{
    char cookie[256];

    size_t len = httpd_req_get_hdr_value_len(req, "Cookie");

    if (len == 0 || len >= sizeof(cookie))
    {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    if (httpd_req_get_hdr_value_str(
            req,
            "Cookie",
            cookie,
            sizeof(cookie)) != ESP_OK)
    {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    // Find the session cookie.
    const char *prefix = "hasp_session=";
    const size_t prefix_len = strlen(prefix);

    const char *p = strstr(cookie, prefix);

    if (!p)
    {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    p += prefix_len;

    // The token must contain exactly SESSION_TOKEN_LEN * 2
    // hexadecimal characters.
    constexpr size_t TOKEN_HEX_LEN = SESSION_TOKEN_LEN * 2;

    if (strlen(p) < TOKEN_HEX_LEN)
    {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    // Decode the hexadecimal cookie value.
    uint8_t token[SESSION_TOKEN_LEN];

    for (size_t i = 0; i < SESSION_TOKEN_LEN; ++i)
    {
        const int hi = hex_value(p[i * 2]);
        const int lo = hex_value(p[i * 2 + 1]);

        if (hi < 0 || lo < 0)
        {
            return ESP_ERR_HTTPD_INVALID_REQ;
        }

        token[i] = static_cast<uint8_t>((hi << 4) | lo);
    }

    const uint32_t now = esp_log_timestamp();

    // Search all active sessions.
    //
    // Do not return immediately on a match. This ensures that every
    // active session token gets compared before returning.
    for (auto &session : sessions_)
    {
        if (!session.active)
        {
            continue;
        }

        // Expire idle sessions.
        if ((now - session.last_activity) > SESSION_TIMEOUT_MS)
        {
            session.active = false;
            continue;
        }

        if (constant_time_equal(
                token,
                session.token,
                SESSION_TOKEN_LEN))
        {
            session.last_activity = now;
            return ESP_OK;
        }
    }

    return ESP_ERR_HTTPD_INVALID_REQ;
}

// -----------------------------------------------------------------------------
// Require authentication
// -----------------------------------------------------------------------------

esp_err_t HaspHttp::require_auth(httpd_req_t *req)
{
    if (authenticate_request(req) == ESP_OK)
    {
        return ESP_OK;
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "text/plain");

    httpd_resp_send(
        req,
        "Authentication required",
        HTTPD_RESP_USE_STRLEN);

    return ESP_FAIL;
}

HaspHttp::~HaspHttp()
{
    stop();
}

// ---------------------------------------------------------------------------
// GET /api/config
// ---------------------------------------------------------------------------
esp_err_t HaspHttp::config_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);

    HaspHttp *self = from_req(req);

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();

    // Collect config from the services we know about
    self->mgr_.get_config(obj);
    // later: self->mqtt_.get_config(obj); etc.

    std::string body;
    serializeJson(doc, body);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body.c_str(), body.size());
    return ESP_OK;
}

esp_err_t HaspHttp::console_get_handler(httpd_req_t *req)
{
        REQUIRE_AUTH(req);

    httpd_resp_set_type(req, "text/html");

    FILE *f = fopen("/littlefs/webui/console.html", "r");
    if (!f)
    {
        ESP_LOGE(TAG, "Failed to open /littlefs/webui/console.html");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "console.html not found");
        return ESP_FAIL;
    }

    char buf[1024];

    while (true)
    {
        size_t n = fread(buf, 1, sizeof(buf), f);

        if (n > 0)
        {
            esp_err_t err = httpd_resp_send_chunk(req, buf, n);
            if (err != ESP_OK)
            {
                fclose(f);
                return err;
            }
        }

        if (n < sizeof(buf))
        {
            if (ferror(f))
            {
                ESP_LOGE(TAG, "Error reading console.html");
                fclose(f);
                return ESP_FAIL;
            }
            break; // EOF
        }
    }

    fclose(f);

    // Terminate chunked response.
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// ---------------------------------------------------------------------------
// POST /api/config
// ---------------------------------------------------------------------------
esp_err_t HaspHttp::config_post_handler(httpd_req_t *req)
{
        REQUIRE_AUTH(req);

    HaspHttp *self = from_req(req);

    // Read body (limit to 2 KB for the MVP)
    char buf[2048];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    JsonObjectConst obj = doc.as<JsonObjectConst>();

    // Apply to the services that are present in the payload
    self->mgr_.set_config(obj);
    // later: mqtt, etc.

    // Return the new full config
    JsonDocument out;
    JsonObject outObj = out.to<JsonObject>();
    self->mgr_.get_config(outObj);

    std::string body;
    serializeJson(out, body);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body.c_str(), body.size());
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// WenSocket /ws
// ---------------------------------------------------------------------------
esp_err_t HaspHttp::ws_auth_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    return ESP_OK;
}

esp_err_t HaspHttp::ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI("WS", "WebSocket handshake complete");
        return ESP_OK;
    }

    ESP_LOGI("WS", "WebSocket frame received");
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;

    // First find payload length
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK)
    {
        return err;
    }

    if (frame.len == 0)
    {
        return ESP_OK;
    }

    frame.payload = static_cast<uint8_t *>(malloc(frame.len + 1));
    if (!frame.payload)
    {
        return ESP_ERR_NO_MEM;
    }

    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err == ESP_OK)
    {
        ((char *)frame.payload)[frame.len] = '\0';

        ESP_LOGI("WS", "RX: %s", (char *)frame.payload);

        // console_execute((char *)frame.payload);
        ESP_LOGI("WS", "type=%d len=%u payload='%.*s'",
                 frame.type,
                 frame.len,
                 frame.len,
                 frame.payload);

        // Echo back the same frame
        frame.type = HTTPD_WS_TYPE_TEXT;
        err = httpd_ws_send_frame(req, &frame);
    }

    free(frame.payload);
    return err;
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------
esp_err_t HaspHttp::start_backend()
{
    if (server_)
        return ESP_OK;
    log_memory();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    // GET /api/config
    httpd_uri_t get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = config_get_handler,
        .user_ctx = this,
        .is_websocket = false,
        .ws_pre_handshake_cb = nullptr};
    err = httpd_register_uri_handler(server_, &get_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register GET /api/config: %s",
                 esp_err_to_name(err));
        return err;
    }

    // POST /api/config
    httpd_uri_t post_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
        .user_ctx = this,
        .is_websocket = false,
        .ws_pre_handshake_cb = nullptr};
    err = httpd_register_uri_handler(server_, &post_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register POST /api/config: %s",
                 esp_err_to_name(err));
        return err;
    }

    httpd_uri_t console_uri = {
        .uri = "/console.html",
        .method = HTTP_GET,
        .handler = console_get_handler,
        .user_ctx = this,
        .is_websocket = false,
        .ws_pre_handshake_cb = nullptr};

    err = httpd_register_uri_handler(server_, &console_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register /console.html: %s",
                 esp_err_to_name(err));
        return err;
    }

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = this,
        .is_websocket = true,
        .ws_pre_handshake_cb = nullptr};

    err = httpd_register_uri_handler(server_, &ws_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register /ws: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "HTTP server started on port 80");
    log_memory();
    return ESP_OK;
}

esp_err_t HaspHttp::stop_backend()
{
    if (!server_)
        return ESP_OK;

    httpd_stop(server_);
    server_ = nullptr;
    ESP_LOGI(TAG, "HTTP server stopped");
    return ESP_OK;
}

bool HaspHttp::isRunning() const
{
    return server_ != nullptr;
}

void HaspHttp::hasp_event_handler(
    void *arg,
    esp_event_base_t base,
    int32_t id,
    void *event_data)
{
    auto *self = static_cast<HaspHttp *>(arg);

    if (id == HASP_EVENT_CONNECTED)
    {
        self->on_network_up();
    }
    else if (id == HASP_EVENT_DISCONNECTED)
    {
        self->on_network_down();
    }
}

void HaspHttp::on_network_up()
{
    switch (mode_)
    {
    case ServiceMode::Never:
    case ServiceMode::Manual:
        return;

    case ServiceMode::Once:
        if (ran_once_)
            return;
        if (start_backend() == ESP_OK)
            ran_once_ = true;
        break;

    case ServiceMode::KeepAlive:
        start_backend();
        break;

    case ServiceMode::OnBoot:
        // already started at boot; optional no-op or ensure running
        if (!isRunning())
            start_backend();
        break;
    }
}

void HaspHttp::on_network_down()
{
    switch (mode_)
    {
    case ServiceMode::KeepAlive:
        stop_backend();
        break;

    case ServiceMode::Once:
    case ServiceMode::OnBoot:
    case ServiceMode::Manual:
    case ServiceMode::Never:
        // leave running (or already stopped)
        break;
    }
}