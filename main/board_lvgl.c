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
    switch (degrees)
    {
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

static esp_err_t
board_lvgl_attach_touch(board_lvgl_display_t *d)
{
    if (!d->touch)
    {
        return ESP_OK;
    }

    if (!d->touch->touch_handle)
    {
        ESP_LOGE(TAG,
                 "Touch device '%s' has no touch handle",
                 d->policy->touch);
        return ESP_ERR_INVALID_STATE;
    }

    esp_lv_adapter_touch_config_t cfg =
        ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(
            d->lv_display,
            d->touch->touch_handle);

    cfg.callbacks = (typeof(cfg.callbacks)){0};

    lv_indev_t *indev = esp_lv_adapter_register_touch(&cfg);
    if (!indev)
    {
        ESP_LOGE(TAG,
                 "Failed to attach touch '%s' to display '%s'",
                 d->policy->touch,
                 d->policy->lcd);
        return ESP_FAIL;
    }

    LV_IMG_DECLARE(mouse_cursor_icon); /*Declare the image file.*/
    lv_obj_t *cursor = lv_image_create(lv_display_get_layer_sys(d->lv_display));
    lv_image_set_src(cursor, &mouse_cursor_icon);
    lv_indev_set_cursor(indev, cursor);

    return ESP_OK;
}

static esp_err_t
board_lvgl_attach_backlight(board_lvgl_display_t *d)
{
    if (!d->backlight)
    {
        return ESP_OK;
    }

    /*
     * Do NOT initialize LEDC here.
     *
     * BMGR already initialized:
     *
     *     ledc_backlight
     *
     * and initialized:
     *
     *     lcd_brightness
     *
     * We merely associate that device with this LVGL display.
     */

    return ESP_OK;
    /* return dev_ledc_ctrl_set_percent(
        d->backlight,
        100); */
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
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_PARLIO) == 0)
    {

        esp_lv_adapter_display_config_t disp_cfg;

        if (policy->use_psram)
        {
            disp_cfg =
                ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
                    lcd->panel_handle,
                    lcd->io_handle,
                    lcd_cfg->lcd_width,
                    lcd_cfg->lcd_height,
                    rotation);
        }
        else
        {
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
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB) == 0)
    {

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
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB_3WIRE_SPI) == 0)
    {

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
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_DSI) == 0)
    {

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

static esp_err_t board_lvgl_resolve_devices(board_lvgl_display_t *d)
{
    esp_err_t ret;
    void *handle = NULL;

    ret = esp_board_manager_get_device_handle(
        d->policy->lcd,
        &handle);
    if (ret != ESP_OK)
    {
        return ret;
    }

    d->lcd = handle;

    ret = esp_board_manager_get_device_config(
        d->policy->lcd,
        (void **)&d->lcd_cfg);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (d->policy->touch && d->policy->touch[0])
    {
        ret = esp_board_manager_get_device_handle(
            d->policy->touch,
            &handle);
        if (ret != ESP_OK)
        {
            return ret;
        }

        d->touch = handle;
    }

    /*
     * Backlight is another BMGR device dependency.
     */
    if (d->policy->backlight &&
        d->policy->backlight[0])
    {

        ret = esp_board_manager_get_device_handle(
            d->policy->backlight,
            &handle);
        if (ret != ESP_OK)
        {
            return ret;
        }

        d->backlight = handle;
    }

    return ESP_OK;
}

static esp_err_t board_lvgl_create_lv_display(board_lvgl_display_t *d)
{
    d->lv_display = board_lvgl_register_display(
        d->lcd,
        d->lcd_cfg,
        d->policy);

    if (!d->lv_display)
    {
        return ESP_FAIL;
    }

    /*
     * esp_lv_adapter owns LVGL user_data.
     *
     * driver_data is our application-owned context.
     */
    lv_display_set_driver_data(
        d->lv_display,
        d);

    return ESP_OK;
}

static esp_err_t board_lvgl_display_create(
    const dev_custom_lvgl_displays_t *policy,
    board_lvgl_display_t **out)
{
    if (!policy || !out)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *out = NULL;

    board_lvgl_display_t *d =
        calloc(1, sizeof(*d));

    if (!d)
    {
        return ESP_ERR_NO_MEM;
    }

    d->policy = policy;

    esp_err_t ret = board_lvgl_resolve_devices(d);
    if (ret != ESP_OK)
    {
        goto fail;
    }

    ret = board_lvgl_create_lv_display(d);
    if (ret != ESP_OK)
    {
        goto fail;
    }

    ret = board_lvgl_attach_touch(d);
    if (ret != ESP_OK)
    {
        goto fail;
    }

    ret = board_lvgl_attach_backlight(d);
    if (ret != ESP_OK)
    {
        goto fail;
    }

    *out = d;
    return ESP_OK;

fail:
    /*
     * TODO: if the LVGL display was successfully created,
     * delete it before freeing d.
     */
    free(d);
    return ret;
}

/*
 * Public entry point.
 *
 * BMGR must already have been initialized before this function is called,
 * because the LCD and touch handles are obtained from BMGR.
 */
esp_err_t board_lvgl_init(void)
{
    dev_custom_lvgl_config_t *cfg = NULL;

    esp_err_t ret =
        esp_board_manager_get_device_config(
            "lvgl",
            (void **)&cfg);

    if (ret != ESP_OK)
    {
        return ret;
    }

    if (!cfg)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_lv_adapter_config_t adapter_cfg =
        ESP_LV_ADAPTER_DEFAULT_CONFIG();

    ret = esp_lv_adapter_init(&adapter_cfg);
    if (ret != ESP_OK)
    {
        return ret;
    }

    for (size_t i = 0;
         i < BOARD_LVGL_ARRAY_SIZE(cfg->displays);
         ++i)
    {

        const dev_custom_lvgl_displays_t *policy =
            &cfg->displays[i];

        if (!policy->lcd || !policy->lcd[0])
        {
            continue;
        }

        board_lvgl_display_t *d = NULL;

        ret = board_lvgl_display_create(policy, &d);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG,
                     "Failed to create LVGL display '%s': %s",
                     policy->lcd,
                     esp_err_to_name(ret));
            return ret;
        }

        ESP_LOGI(TAG,
                 "LVGL display #%zu '%s' initialized",
                 i,
                 policy->lcd);
    }

    return esp_lv_adapter_start();
}
