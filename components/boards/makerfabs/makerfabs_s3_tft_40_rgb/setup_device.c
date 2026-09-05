#include "esp_err.h"
#include "esp_check.h"

#include "esp_board_manager.h"
#include "esp_board_device.h"
#include "esp_board_periph.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"

#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"

#include "st7701_type1_init.h"

static const esp_lcd_rgb_panel_config_t makerfabs_rgb_config = {
    .clk_src = LCD_CLK_SRC_PLL160M,

    .timings = {
        .pclk_hz = 14000000,

        .h_res = 480,
        .v_res = 480,

        .hsync_pulse_width = 8,
        .hsync_back_porch = 50,
        .hsync_front_porch = 10,

        .vsync_pulse_width = 8,
        .vsync_back_porch = 20,
        .vsync_front_porch = 10,

        .flags = {
            .hsync_idle_low = false,
            .vsync_idle_low = false,
            .de_idle_high = false,
            .pclk_active_neg = true,
            .pclk_idle_high = false,
        },
    },

    .data_width = 16,
    .num_fbs = 3,
    .bounce_buffer_size_px = 0,
    .dma_burst_size = 64,

    .hsync_gpio_num = 5,
    .vsync_gpio_num = 4,
    .de_gpio_num = 45,
    .pclk_gpio_num = 21,
    .disp_gpio_num = -1,

    .data_gpio_nums = {
        6, 7, 15, 16, 8, 0, 9, 14, 47, 48, 3, 39, 40, 41, 42, 2
    },

    .flags = {
        .disp_active_low = false,
        .refresh_on_demand = false,
        .fb_in_psram = true,
        .double_fb = false,
        .no_fb = false,
        .bb_invalidate_cache = false,
    },

    .in_color_format = LCD_COLOR_FMT_RGB565,
    .out_color_format = LCD_COLOR_FMT_RGB565,
};

static st7701_vendor_config_t makerfabs_st7701_vendor_config = {
    .rgb_config = &makerfabs_rgb_config,

    .init_cmds = st7701_type1_init_cmds,
    .init_cmds_size = st7701_type1_init_cmds_count,

    .flags = {
        .use_mipi_interface = 0,
        .mirror_by_cmd = 0,
        .auto_del_panel_io = 0,
    },
};

esp_err_t lcd_panel_factory_entry_t(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(
        io != NULL &&
        panel_dev_config != NULL &&
        ret_panel != NULL,
        ESP_ERR_INVALID_ARG,
        "makerfabs_s3_tft_40_rgb",
        "invalid LCD factory arguments"
    );

    esp_lcd_panel_dev_config_t config = *panel_dev_config;

    /*
     * BMGR's vendor_config is not our source of truth.
     *
     * The ST7701 driver requires:
     *
     *   panel_dev_config.vendor_config
     *       -> st7701_vendor_config_t
     *           -> rgb_config
     *               -> esp_lcd_rgb_panel_config_t
     *
     * Construct that contract here with the known-good
     * Makerfabs hardware configuration.
     */
    config.vendor_config = &makerfabs_st7701_vendor_config;

    return esp_lcd_new_panel_st7701(
        io,
        &config,
        ret_panel
    );
}

esp_err_t lcd_touch_factory_entry_t(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *touch_dev_config,
    esp_lcd_touch_handle_t *ret_touch)
{
    return esp_lcd_touch_new_i2c_gt911(
        io,
        touch_dev_config,
        ret_touch);
}