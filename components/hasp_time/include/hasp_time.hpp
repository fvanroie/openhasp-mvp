#pragma once

#include "hasp_event.hpp"
#include "hasp_service.hpp"
#include "hasp_service_manager.hpp"

#include <string>
#include <ctime>
#include <sys/time.h>

/**
 * SNTP time synchronisation service.
 *
 * Follows the same shape as HaspMqtt / HaspHttp:
 *   - registers for HASP_EVENT_CONNECTED / HASP_EVENT_DISCONNECTED
 *   - starts the SNTP client when the network comes up (ServiceMode::KeepAlive)
 *   - stores its configuration in NVS under the "time" namespace
 *   - publishes HASP_EVENT_TIME_UPDATED after every successful sync
 *
 * JSON configuration (namespace / key "time"):
 *   {
 *     "time": {
 *       "server":   "pool.ntp.org",
 *       "server2":  "time.nist.gov",
 *       "server3":  "",
 *       "tz":       "CET-1CEST,M3.5.0,M10.5.0/3",
 *       "interval": 60          // minutes between re-syncs
 *     }
 *   }
 */
class HaspTime : public HaspService {
public:
    explicit HaspTime(ServiceManager& mgr) : mgr_(mgr) {
        mode_ = ServiceMode::KeepAlive;   // start on GOT_IP, stop on disconnect

        esp_event_handler_instance_register(
            HASP_EVENT, HASP_EVENT_CONNECTED,
            &HaspTime::hasp_event_handler, this, nullptr);

        esp_event_handler_instance_register(
            HASP_EVENT, HASP_EVENT_DISCONNECTED,
            &HaspTime::hasp_event_handler, this, nullptr);
    }
    ~HaspTime() override;

    const char* name() const override { return "time"; }

    bool isRunning() const override;

    esp_err_t get_config(JsonObject obj) const override;
    esp_err_t set_config(JsonObjectConst obj) override;

    /** Force an immediate SNTP request (does not wait for the next interval). */
    esp_err_t syncNow();

    /** True once at least one valid NTP answer has been applied. */
    bool isSynced() const { return synced_; }

    /** Seconds since the Unix epoch, 0 when the clock is not valid yet. */
    static time_t epoch();

    /** True when the system clock looks plausible (after 2020-01-01 UTC). */
    static bool isValid();

    /** Current local time broken down using the configured TZ. */
    static struct tm localTime();

    /** Current local time as "YYYY-MM-DDTHH:MM:SS". */
    static std::string iso8601();

protected:
    esp_err_t start_backend() override;
    esp_err_t stop_backend() override;

    void on_network_up() override;
    void on_network_down() override;

private:
    static constexpr size_t   MAX_SERVERS      = 3;
    static constexpr uint32_t DEFAULT_INTERVAL = 60;   // minutes

    ServiceManager& mgr_;

    bool started_ = false;
    bool synced_  = false;

    std::string servers_[MAX_SERVERS];
    std::string tz_;
    uint32_t    interval_min_ = DEFAULT_INTERVAL;

    esp_err_t load_from_nvs();
    void      apply_timezone() const;

    /** lwIP SNTP notification – runs in the tcpip thread context. */
    static void time_sync_cb(struct timeval* tv);
    void        on_time_synced(struct timeval* tv);

    static void hasp_event_handler(
        void* arg,
        esp_event_base_t base,
        int32_t id,
        void* event_data);
};
