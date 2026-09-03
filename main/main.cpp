#include "nvs_flash.h"
#include "esp_log.h"

#include "esp_board_manager.h"
#include "esp_board_device.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
// or the generated / device headers that declare the structs
#include "dev_display_lcd.h" // for dev_display_lcd_handles_t
#include "dev_lcd_touch.h"   // for the touch handle type (if needed)

#include "esp_lv_adapter.h"
#include "board_lvgl.h"
#include "lvgl.h"

#include "hasp_fs.hpp"
#include "hasp_ftp.hpp"
#include "hasp_http.hpp"
#include "hasp_log.hpp"
#include "hasp_mqtt.hpp"
#include "hasp_service_manager.hpp"
#include "hasp_wifi.hpp"

static const char *TAG = "main";

static ServiceManager mgr;
static HaspWifi wifi;
static HaspFtp ftp(mgr);   // only knows the manager
static HaspHttp http(mgr); // only knows the manager
static HaspMqtt mqtt(mgr); // only knows the manager

extern const esp_board_device_desc_t g_esp_board_devices[];

static void lvgl_log_cb(lv_log_level_t level, const char *buf)
{
    /*
    LV_LOG_LEVEL_TRACE: A lot of logs to give detailed information
    LV_LOG_LEVEL_INFO: Log important events.
    LV_LOG_LEVEL_WARN: Log if something unwanted happened but didn't cause a problem.
    LV_LOG_LEVEL_ERROR: Log only critical issues, where the system may fail.
    LV_LOG_LEVEL_USER: Log only custom log messages added by the user.
    */
    esp_log_level_t log_level = ESP_LOG_VERBOSE;

    // Routes directly through the standard formatter engine with an LVGL tag label
    switch (level)
    {
    case LV_LOG_LEVEL_TRACE:
        log_level = ESP_LOG_DEBUG;
        break;
    case LV_LOG_LEVEL_INFO:
        log_level = ESP_LOG_INFO;
        break;
    case LV_LOG_LEVEL_WARN:
        log_level = ESP_LOG_WARN;
        break;
    case LV_LOG_LEVEL_ERROR:
        log_level = ESP_LOG_ERROR;
        break;
    default:
        log_level = ESP_LOG_VERBOSE;
        break;
    }

    hasp_log_printf_with_tag(log_level, "LVGL", "%s", buf);
}

static void app_ui_init()
{
    if (esp_lv_adapter_lock(-1) == ESP_OK)
    {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x00FFFF), 0);

        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text(label, "openHASP + LVGL9");
        lv_obj_center(label);

        lv_obj_t *button = lv_button_create(scr);

        lv_obj_set_pos(button, 50, 50);
        lv_obj_set_size(button, 100, 50);

        label = lv_label_create(button);
        lv_label_set_text(label, "Button");
        lv_obj_center(label);

        esp_lv_adapter_unlock();
    }
}

extern "C" void app_main()
{
    hasp_log_init();

    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(hasp_fs_init()); // before network services
                        
    // Registration order = start order
    mgr.add(&wifi);
    mgr.add(&http);
    mgr.add(&mqtt);
    mgr.add(&ftp);

    // First-time credentials (comment out after first run)
    {
        JsonDocument doc;
        doc["wifi"]["ssid"] = "YOUR_SSID";
        doc["wifi"]["password"] = "YOUR_PASSWORD";
        doc["wifi"]["hostname"] = "openhasp";
        // mgr.set_config(doc.as<JsonObject>());
    }

    {
        JsonDocument doc;
        doc["mqtt"]["host"] = "homeassistant.local";
        doc["mqtt"]["port"] = 1883;
        doc["mqtt"]["user"] = "YOUR_USERNAME";
        doc["mqtt"]["password"] = "YOUR_PASSWORD";
        doc["mqtt"]["client_id"] = "plate01-mvp";
        // mgr.set_config(doc.as<JsonObject>());
    }

    esp_board_manager_print_board_info();
    ESP_ERROR_CHECK(esp_board_manager_init());
    esp_board_manager_print();

    ESP_ERROR_CHECK(board_lvgl_init());
    lv_log_register_print_cb(lvgl_log_cb);

    app_ui_init();

    // ---------- Backlight ----------
    // WT32-SC01 Plus usually has backlight on GPIO 45 (check your board YAML / schematic)

    // gpio_config_t bl_conf = {
    //     .pin_bit_mask = 1ULL << LCD_BL_GPIO,
    //     .mode = GPIO_MODE_OUTPUT,
    //     .pull_up_en = GPIO_PULLUP_DISABLE,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .intr_type = GPIO_INTR_DISABLE,
    // };
    // gpio_config(&bl_conf);
    // gpio_set_level((gpio_num_t)LCD_BL_GPIO, 1);

    // // ---------- Fill screen CYAN ----------
    // const int width = 320; // or 480 depending on orientation
    // const int height = 480;

    // // CYAN in RGB565 = 0x07FF
    // uint16_t *line = (uint16_t *)heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_DMA);
    // if (line)
    // {
    //     for (int i = 0; i < width; i++)
    //     {
    //         // 0xF81F;         // Cyan
    //         line[i] = (0xF81F); // Cyan, byte-swapped for little-endian
    //     }
    //     for (int y = 0; y < height; y++)
    //     {
    //         esp_lcd_panel_draw_bitmap(panel, 0, y, width, y + 1, line);
    //     }
    //     free(line);
    // }

    // ESP_LOGI("main", "Screen should now be CYAN with backlight on");


    mgr.startAll(); // wifi first, then http

    // From here the LVGL task runs; app_main can return or do other work

    // Wait until we have an IP (simple polling for MVP)
    while (!wifi.isConnected())
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "IP: %s", wifi.getIp().c_str());
}
