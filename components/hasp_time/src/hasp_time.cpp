#include "hasp_time.hpp"

#include "esp_log.h"
#include "esp_sntp.h"

#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>

static const char* TAG = "HASP_TIME";

// Single instance – the SNTP notification callback is a plain C function
// pointer, so we keep a back-pointer to the service object here.
static HaspTime* s_instance = nullptr;

// Sanity threshold: anything before 2020-01-01 UTC means "clock not set".
static constexpr time_t EPOCH_2020 = 1577836800;

HaspTime::~HaspTime()
{
    stop();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

esp_err_t HaspTime::get_config(JsonObject obj) const
{
    JsonObject cfg = obj[name()].to<JsonObject>();

    std::string server, server2, server3, tz;
    uint32_t interval = interval_min_;

    if (!servers_[0].empty()) server = servers_[0];
    else nvs_get_string("server", server);

    if (!servers_[1].empty()) server2 = servers_[1];
    else nvs_get_string("server2", server2);

    if (!servers_[2].empty()) server3 = servers_[2];
    else nvs_get_string("server3", server3);

    if (!tz_.empty()) tz = tz_;
    else nvs_get_string("tz", tz);

    if (interval == 0) {
        nvs_get_u32("interval", interval);
    }
    if (interval == 0) {
        interval = DEFAULT_INTERVAL;
    }

    cfg["server"]   = server;
    cfg["server2"]  = server2;
    cfg["server3"]  = server3;
    cfg["tz"]       = tz;
    cfg["interval"] = interval;

    return ESP_OK;
}

esp_err_t HaspTime::set_config(JsonObjectConst obj)
{
    JsonObjectConst cfg = obj[name()] | obj;

    static const char* server_keys[MAX_SERVERS] = {"server", "server2", "server3"};

    for (size_t i = 0; i < MAX_SERVERS; ++i) {
        if (cfg[server_keys[i]].is<const char*>()) {
            servers_[i] = cfg[server_keys[i]].as<std::string>();
            nvs_set_string(server_keys[i], servers_[i]);
        }
    }

    if (cfg["tz"].is<const char*>()) {
        tz_ = cfg["tz"].as<std::string>();
        nvs_set_string("tz", tz_);
        apply_timezone();
    }

    if (cfg["interval"].is<uint32_t>() || cfg["interval"].is<int>()) {
        interval_min_ = cfg["interval"].as<uint32_t>();
        if (interval_min_ == 0) {
            interval_min_ = DEFAULT_INTERVAL;
        }
        nvs_set_u32("interval", interval_min_);

        // Apply immediately when SNTP is already running.
        if (started_) {
            esp_sntp_set_sync_interval(interval_min_ * 60 * 1000);
        }
    }

    return ESP_OK;
}

esp_err_t HaspTime::load_from_nvs()
{
    static const char* server_keys[MAX_SERVERS] = {"server", "server2", "server3"};

    for (size_t i = 0; i < MAX_SERVERS; ++i) {
        nvs_get_string(server_keys[i], servers_[i]);
    }

    if (servers_[0].empty()) {
        servers_[0] = "pool.ntp.org";   // sensible default
    }

    nvs_get_string("tz", tz_);
    nvs_get_u32("interval", interval_min_);
    if (interval_min_ == 0) {
        interval_min_ = DEFAULT_INTERVAL;
    }

    return ESP_OK;
}

void HaspTime::apply_timezone() const
{
    // POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3".
    // Empty string (or unset) keeps the system clock on UTC.
    setenv("TZ", tz_.c_str(), 1);
    tzset();
}

// ---------------------------------------------------------------------------
// Backend start/stop
// ---------------------------------------------------------------------------

esp_err_t HaspTime::start_backend()
{
    if (started_) {
        return ESP_OK;
    }

    load_from_nvs();
    apply_timezone();

    s_instance = this;

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_set_sync_interval(interval_min_ * 60 * 1000);

    // lwIP only accepts as many servers as CONFIG_LWIP_SNTP_MAX_SERVERS allows.
    size_t max_servers = MAX_SERVERS;
#ifdef SNTP_MAX_SERVERS
    if (max_servers > SNTP_MAX_SERVERS) {
        max_servers = SNTP_MAX_SERVERS;
    }
#endif

    size_t configured = 0;
    for (size_t i = 0; i < max_servers; ++i) {
        if (servers_[i].empty()) {
            continue;
        }
        esp_sntp_setservername(static_cast<u8_t>(configured), servers_[i].c_str());
        ESP_LOGI(TAG, "NTP server[%u] = %s", (unsigned)configured, servers_[i].c_str());
        ++configured;
    }

    if (configured == 0) {
        ESP_LOGW(TAG, "No NTP server configured");
        s_instance = nullptr;
        return ESP_ERR_INVALID_STATE;
    }

    esp_sntp_init();
    started_ = true;

    ESP_LOGI(TAG, "SNTP started, re-sync every %u min", (unsigned)interval_min_);
    return ESP_OK;
}

esp_err_t HaspTime::stop_backend()
{
    if (!started_) {
        return ESP_OK;
    }

    esp_sntp_stop();
    started_ = false;

    if (s_instance == this) {
        s_instance = nullptr;
    }

    ESP_LOGI(TAG, "SNTP stopped");
    return ESP_OK;
}

esp_err_t HaspTime::syncNow()
{
    if (!started_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!esp_sntp_restart()) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Manual re-sync requested");
    return ESP_OK;
}

bool HaspTime::isRunning() const
{
    return started_;
}

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------

time_t HaspTime::epoch()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec;
}

bool HaspTime::isValid()
{
    return epoch() > EPOCH_2020;
}

struct tm HaspTime::localTime()
{
    time_t now = epoch();
    struct tm timeinfo = {};
    localtime_r(&now, &timeinfo);
    return timeinfo;
}

std::string HaspTime::iso8601()
{
    struct tm t = localTime();
    char buf[24];   // "YYYY-MM-DDTHH:MM:SS" + NUL
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &t);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// SNTP notification
// ---------------------------------------------------------------------------

void HaspTime::time_sync_cb(struct timeval* tv)
{
    if (s_instance) {
        s_instance->on_time_synced(tv);
    }
}

void HaspTime::on_time_synced(struct timeval* tv)
{
    (void)tv;

    if (!synced_) {
        synced_ = true;
        ESP_LOGI(TAG, "NTP time synchronised: %s", iso8601().c_str());
    } else {
        ESP_LOGD(TAG, "NTP time updated: %s", iso8601().c_str());
    }

    // Let the rest of the firmware know the clock can be trusted now.
    esp_event_post(HASP_EVENT, HASP_EVENT_TIME_UPDATED, nullptr, 0, 0);
}

// ---------------------------------------------------------------------------
// HASP events (network)
// ---------------------------------------------------------------------------

void HaspTime::hasp_event_handler(void* arg, esp_event_base_t, int32_t id, void*)
{
    auto* self = static_cast<HaspTime*>(arg);
    if (id == HASP_EVENT_CONNECTED) {
        self->on_network_up();
    } else if (id == HASP_EVENT_DISCONNECTED) {
        self->on_network_down();
    }
}

void HaspTime::on_network_up()
{
    switch (mode_) {
    case ServiceMode::Never:
    case ServiceMode::Manual:
        return;

    case ServiceMode::Once:
        if (ran_once_) {
            return;
        }
        if (start_backend() == ESP_OK) {
            ran_once_ = true;
        }
        break;

    case ServiceMode::KeepAlive:
        start_backend();
        break;

    case ServiceMode::OnBoot:
        if (!isRunning()) {
            start_backend();
        }
        break;
    }
}

void HaspTime::on_network_down()
{
    switch (mode_) {
    case ServiceMode::KeepAlive:
        stop_backend();
        break;

    case ServiceMode::Once:
    case ServiceMode::OnBoot:
    case ServiceMode::Manual:
    case ServiceMode::Never:
        // leave the clock running – SNTP simply retries when the link is back
        break;
    }
}
