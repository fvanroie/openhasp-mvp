/*
 * SPDX-FileCopyrightText: 2026 Franis Van Roie / openHASP
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is based on or incorporates material from the Espressif Systems 
 * esp-board-manager example (main.c), licensed under the Apache License 2.0.
 *
 * Changes made:
 * - Refactored into a custom BMGR device abstraction layer (board_lvgl).
 * - Added YAML policy structures for resolution and rotation mapping.
 */


#include "board_lvgl.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "esp_board_manager.h"
#include "esp_lv_adapter.h"

#include "dev_display_lcd.h"
#include "dev_lcd_touch.h"
#include "gen_board_device_custom.h"

static const char *TAG = "board_lvgl";


#define BOARD_LVGL_ARRAY_SIZE(a) \
    (sizeof(a) / sizeof((a)[0]))


/*
 * Convert the board YAML's integer rotation into the adapter enum.
 *
 * Keep this mapping here rather than putting esp_lv_adapter types into
 * the BMGR-generated configuration structure.
 */
static esp_lv_adapter_rotation_t board_lvgl_rotation_from_degrees(int16_t degrees)
{
    switch (degrees) {
    case 0:
        return ESP_LV_ADAPTER_ROTATE_0;

    case 90:
        return ESP_LV_ADAPTER_ROTATE_90;

    case 180:
        return ESP_LV_ADAPTER_ROTATE_180;

    case 270:
        return ESP_LV_ADAPTER_ROTATE_270;

    default:
        ESP_LOGE(TAG, "Unsupported LVGL rotation: %d", degrees);
        return ESP_LV_ADAPTER_ROTATE_0;
    }
}


/*
 * Register one BMGR LCD device with esp_lv_adapter.
 *
 * This is intentionally very close to Espressif's BMGR LVGL example:
 *
 *   display_lcd config
 *       |
 *       +-- SPI / I80 / PARLIO
 *       +-- RGB
 *       +-- RGB 3-wire SPI
 *       +-- DSI
 *
 * The LVGL policy supplies the buffer/rotation settings that are specific
 * to this LVGL display instance.
 */
static lv_display_t *board_lvgl_register_display(
    const dev_display_lcd_handles_t *lcd,
    const dev_display_lcd_config_t *lcd_cfg,
    const dev_custom_lvgl_displays_t *policy)
{
    assert(lcd != NULL);
    assert(lcd_cfg != NULL);
    assert(policy != NULL);

    const esp_lv_adapter_rotation_t rotation =
        board_lvgl_rotation_from_degrees(policy->rotation);

    /*
     * SPI/I80/PARLIO use the "OTHER" adapter path.
     *
     * This is also the path used by the adapter for SPI/I2C/I80/QSPI.
     */
    if (strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_SPI) == 0 ||
        strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_I80) == 0 ||
        strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_PARLIO) == 0) {

        esp_lv_adapter_display_config_t disp_cfg;

        if (policy->use_psram) {
            disp_cfg =
                ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
                    lcd->panel_handle,
                    lcd->io_handle,
                    lcd_cfg->lcd_width,
                    lcd_cfg->lcd_height,
                    rotation);
        } else {
            disp_cfg =
                ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(
                    lcd->panel_handle,
                    lcd->io_handle,
                    lcd_cfg->lcd_width,
                    lcd_cfg->lcd_height,
                    rotation);
        }

        /*
         * Override the adapter's default buffer policy with the board
         * policy.
         */
        disp_cfg.profile.buffer_height = policy->buffer_height;
        disp_cfg.profile.require_double_buffer = policy->double_buffer;
        disp_cfg.profile.use_psram = policy->use_psram;

        /*
         * OTHER interfaces only support NONE tearing avoidance.
         */
        disp_cfg.tear_avoid_mode =
            ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE;

        return esp_lv_adapter_register_display(&disp_cfg);
    }


    /*
     * RGB panel.
     */
    if (strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB) == 0) {

#if CONFIG_BOARD_LVGL_USE_RGB

        esp_lv_adapter_display_config_t disp_cfg =
            ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
                lcd->panel_handle,
                lcd->io_handle,
                lcd_cfg->lcd_width,
                lcd_cfg->lcd_height,
                rotation);

        disp_cfg.profile.buffer_height = policy->buffer_height;
        disp_cfg.profile.require_double_buffer = policy->double_buffer;
        disp_cfg.profile.use_psram = policy->use_psram;

        return esp_lv_adapter_register_display(&disp_cfg);

#else

        ESP_LOGE(TAG,
                 "LCD '%s' is RGB, but "
                 "CONFIG_BOARD_LVGL_USE_RGB is disabled",
                 policy->lcd);
        return NULL;

#endif
    }


    /*
     * RGB panel controlled through 3-wire SPI.
     *
     * The adapter treats this as the RGB display path.
     */
    if (strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB_3WIRE_SPI) == 0) {

#if CONFIG_BOARD_LVGL_USE_RGB_3WIRE_SPI

        esp_lv_adapter_display_config_t disp_cfg =
            ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
                lcd->panel_handle,
                lcd->io_handle,
                lcd_cfg->lcd_width,
                lcd_cfg->lcd_height,
                rotation);

        disp_cfg.profile.buffer_height = policy->buffer_height;
        disp_cfg.profile.require_double_buffer = policy->double_buffer;
        disp_cfg.profile.use_psram = policy->use_psram;

        return esp_lv_adapter_register_display(&disp_cfg);

#else

        ESP_LOGE(TAG,
                 "LCD '%s' is RGB 3-wire SPI, but "
                 "CONFIG_BOARD_LVGL_USE_RGB_3WIRE_SPI is disabled",
                 policy->lcd);
        return NULL;

#endif
    }


    /*
     * MIPI DSI.
     */
    if (strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_DSI) == 0) {

#if CONFIG_BOARD_LVGL_USE_DSI

        esp_lv_adapter_display_config_t disp_cfg =
            ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
                lcd->panel_handle,
                lcd->io_handle,
                lcd_cfg->lcd_width,
                lcd_cfg->lcd_height,
                rotation);

        disp_cfg.profile.buffer_height = policy->buffer_height;
        disp_cfg.profile.require_double_buffer = policy->double_buffer;
        disp_cfg.profile.use_psram = policy->use_psram;

        return esp_lv_adapter_register_display(&disp_cfg);

#else

        ESP_LOGE(TAG,
                 "LCD '%s' is MIPI DSI, but "
                 "CONFIG_BOARD_LVGL_USE_DSI is disabled",
                 policy->lcd);
        return NULL;

#endif
    }


    ESP_LOGE(TAG,
             "LCD '%s' has unsupported subtype '%s'",
             policy->lcd,
             lcd_cfg->sub_type);

    return NULL;
}


/*
 * Register the optional touch device belonging to one LVGL display.
 */
static esp_err_t board_lvgl_register_touch(
    const dev_custom_lvgl_displays_t *policy,
    lv_display_t *display)
{
    assert(policy != NULL);
    assert(display != NULL);

    /*
     * An empty touch mapping means "this display has no touch".
     */
    if (policy->touch == NULL || policy->touch[0] == '\0') {
        ESP_LOGI(TAG,
                 "LVGL display '%s' has no touch device",
                 policy->lcd);
        return ESP_OK;
    }

    void *touch_handle = NULL;

    esp_err_t ret = esp_board_manager_get_device_handle(
        policy->touch,
        &touch_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to get touch device '%s': %s",
                 policy->touch,
                 esp_err_to_name(ret));
        return ret;
    }

    if (touch_handle == NULL) {
        ESP_LOGE(TAG,
                 "Touch device '%s' returned NULL handle",
                 policy->touch);
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * BMGR's lcd_touch device handle contains the actual
     * esp_lcd_touch_handle_t.
     */
    dev_lcd_touch_handles_t *touch =
        (dev_lcd_touch_handles_t *)touch_handle;

    if (touch->touch_handle == NULL) {
        ESP_LOGE(TAG,
                 "Touch device '%s' has NULL touch_handle",
                 policy->touch);
        return ESP_ERR_INVALID_STATE;
    }

    esp_lv_adapter_touch_config_t touch_cfg =
        ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(
            display,
            touch->touch_handle);

    /*
     * Keep callbacks explicitly zeroed for adapter versions/configurations
     * where this field exists.
     */
    touch_cfg.callbacks = (typeof(touch_cfg.callbacks)){0};

    lv_indev_t *indev =
        esp_lv_adapter_register_touch(&touch_cfg);

    if (indev == NULL) {
        ESP_LOGE(TAG,
                 "Failed to register touch '%s' for display '%s'",
                 policy->touch,
                 policy->lcd);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "Registered touch '%s' -> display '%s'",
             policy->touch,
             policy->lcd);

    return ESP_OK;
}


/*
 * Public entry point.
 *
 * BMGR must already have been initialized before this function is called,
 * because the LCD and touch handles are obtained from BMGR.
 */
esp_err_t board_lvgl_init(void)
{
    dev_custom_lvgl_config_t *lvgl_cfg = NULL;

    esp_err_t ret = esp_board_manager_get_device_config(
        "lvgl",
        (void **)&lvgl_cfg);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to get LVGL policy configuration: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    if (lvgl_cfg == NULL) {
        ESP_LOGE(TAG, "LVGL policy configuration is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG,
             "Initializing LVGL adapter for %zu display(s)",
             BOARD_LVGL_ARRAY_SIZE(lvgl_cfg->displays));


    /*
     * ---------------------------------------------------------------
     * 1. Initialize the adapter exactly once.
     * ---------------------------------------------------------------
     */
    esp_lv_adapter_config_t adapter_cfg =
        ESP_LV_ADAPTER_DEFAULT_CONFIG();

    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_cfg));


    /*
     * ---------------------------------------------------------------
     * 2. Register every LVGL display mapping.
     * ---------------------------------------------------------------
     */
    for (size_t i = 0;
         i < BOARD_LVGL_ARRAY_SIZE(lvgl_cfg->displays);
         ++i) {

        const dev_custom_lvgl_displays_t *policy =
            &lvgl_cfg->displays[i];

        /*
         * A NULL LCD name is an unused generated entry.
         *
         * This makes the implementation safe if BMGR ever generates
         * a fixed-capacity array rather than an exact-sized array.
         */
        if (policy->lcd == NULL || policy->lcd[0] == '\0') {
            continue;
        }

        ESP_LOGI(TAG,
                 "Registering LVGL display #%zu: lcd='%s', touch='%s', "
                 "rotation=%d, double_buffer=%d, use_psram=%d, "
                 "buffer_height=%d",
                 i,
                 policy->lcd,
                 policy->touch ? policy->touch : "(none)",
                 policy->rotation,
                 policy->double_buffer,
                 policy->use_psram,
                 policy->buffer_height);


        /*
         * Get the BMGR LCD device.
         */
        void *lcd_handle = NULL;

        ret = esp_board_manager_get_device_handle(
            policy->lcd,
            &lcd_handle);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "Failed to get LCD device '%s': %s",
                     policy->lcd,
                     esp_err_to_name(ret));
            return ret;
        }

        if (lcd_handle == NULL) {
            ESP_LOGE(TAG,
                     "LCD device '%s' returned NULL handle",
                     policy->lcd);
            return ESP_ERR_INVALID_STATE;
        }

        dev_display_lcd_handles_t *lcd =
            (dev_display_lcd_handles_t *)lcd_handle;


        /*
         * Get the BMGR LCD configuration.  This tells us which
         * esp_lcd/adapter path to select.
         */
        dev_display_lcd_config_t *lcd_cfg = NULL;

        ret = esp_board_manager_get_device_config(
            policy->lcd,
            (void **)&lcd_cfg);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "Failed to get LCD config '%s': %s",
                     policy->lcd,
                     esp_err_to_name(ret));
            return ret;
        }

        if (lcd_cfg == NULL) {
            ESP_LOGE(TAG,
                     "LCD config '%s' is NULL",
                     policy->lcd);
            return ESP_ERR_INVALID_STATE;
        }


        /*
         * Register the physical LCD as an LVGL display.
         */
        lv_display_t *display =
            board_lvgl_register_display(
                lcd,
                lcd_cfg,
                policy);

        if (display == NULL) {
            ESP_LOGE(TAG,
                     "Failed to register LVGL display #%zu "
                     "from LCD '%s'",
                     i,
                     policy->lcd);
            return ESP_FAIL;
        }


        /*
         * Register the touch device associated with this display,
         * if the mapping specifies one.
         */
        ret = board_lvgl_register_touch(policy, display);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "Failed to register touch for LVGL display #%zu",
                     i);
            return ret;
        }

        ESP_LOGI(TAG,
                 "LVGL display #%zu registered successfully",
                 i);
    }


    /*
     * ---------------------------------------------------------------
     * 3. Start the adapter exactly once, after all displays and
     *    input devices have been registered.
     * ---------------------------------------------------------------
     */
    ret = esp_lv_adapter_start();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to start LVGL adapter: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LVGL adapter started successfully");

    return ESP_OK;
}
