#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"

#include "dev_display_lcd.h"
#include "dev_lcd_touch.h"
#include "dev_ledc_ctrl.h"
#include "gen_board_device_custom.h"

typedef struct {
    /* Immutable board policy for this LVGL display. */
    const dev_custom_lvgl_displays_t *policy;

    /* BMGR-resolved device objects. */
    dev_display_lcd_handles_t *lcd;
    dev_display_lcd_config_t  *lcd_cfg;
    dev_lcd_touch_handles_t   *touch;

    /* Optional backlight BMGR object. */
    void   *backlight;

    /* LVGL object created from the above. */
    lv_display_t *lv_display;

} board_lvgl_display_t;

esp_err_t board_lvgl_init(void);

#ifdef __cplusplus
}
#endif