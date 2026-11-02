/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_board_manager_includes.h"

#include "jd9365_v2_init.h"

#if __has_include(<esp_lcd_jd9365.h>)
#define HAS_JD9365 1
#include <esp_lcd_jd9365.h>
#endif

static const char *TAG = "JC8012P4A1C_I_W_Y_V2";

#if defined(HAS_JD9365)

__attribute__((weak))
esp_err_t lcd_dsi_panel_factory_entry_t(
    esp_lcd_dsi_bus_handle_t dsi_handle,
    dev_display_lcd_config_t *lcd_cfg,
    dev_display_lcd_handles_t *lcd_handles)
{
    if (dsi_handle == NULL) {
        ESP_LOGE(TAG, "DSI bus handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (lcd_cfg == NULL || lcd_handles == NULL) {
        ESP_LOGE(TAG, "Invalid LCD configuration/handles");
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * The DPI configuration itself comes from board_devices.yaml.
     *
     * For this board:
     *   800 x 1280
     *   80 MHz
     *   H: 20 / 20 / 40
     *   V: 10 / 4 / 30
     *   RGB565
     */
    jd9365_vendor_config_t vendor_config = {
        .init_cmds = jc8012p4a1_v2_init_cmds,
        .init_cmds_size =
            sizeof(jc8012p4a1_v2_init_cmds) /
            sizeof(jc8012p4a1_v2_init_cmds[0]),

        .mipi_config = {
            .dsi_bus = dsi_handle,
            .dpi_config = &lcd_cfg->sub_cfg.dsi.dpi_config,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num =
            lcd_cfg->sub_cfg.dsi.reset_gpio_num,

        .rgb_ele_order =
            lcd_cfg->rgb_ele_order,

        .bits_per_pixel =
            lcd_cfg->bits_per_pixel,

        .data_endian =
            lcd_cfg->data_endian,

        .flags = {
            .reset_active_high =
                lcd_cfg->sub_cfg.dsi.reset_active_high,
        },

        .vendor_config = &vendor_config,
    };

    ESP_LOGI(TAG,
             "Creating JD9365 V2: 800x1280 RGB565, "
             "DSI 2-lane @ 1500 Mbps/lane, DPI 80 MHz");

    esp_err_t ret = esp_lcd_new_panel_jd9365(
        lcd_handles->io_handle,
        &panel_config,
        &lcd_handles->panel_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to create JD9365 panel: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

#else

#error "esp_lcd_jd9365 component is required for JC8012P4A1C_I_W_Y_V2"

#endif
