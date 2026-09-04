#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"

#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"

esp_err_t lcd_panel_factory_entry_t(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel)
{
    // Create a local copy of the configuration
    esp_lcd_panel_dev_config_t local_panel_config = *panel_dev_config;

    st7796_vendor_config_t st7796_vendor_cfg = {
        .flags = {
            .use_mipi_interface = false
        }
    };

    local_panel_config.vendor_config = &st7796_vendor_cfg;

    return esp_lcd_new_panel_st7796(
        io,
        &local_panel_config,
        ret_panel
    );
}

esp_err_t lcd_touch_factory_entry_t(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *touch_dev_config,
    esp_lcd_touch_handle_t *ret_touch)
{
    return esp_lcd_touch_new_i2c_ft5x06(
        io,
        touch_dev_config,
        ret_touch);
}
