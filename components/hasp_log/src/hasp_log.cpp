#include "hasp_log.hpp"

#include <stdio.h>
#include <stdarg.h>
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

#define HASP_LOG_UART_PORT      UART_NUM_0
#define HASP_LOG_BUFFER_SIZE    384  // Allocate large enough array to handle full log formatting strings

static const char* TAG = "HASP_LOG";

static bool s_ready = false;
static volatile hasp_log_dest_t s_destination = HASP_LOG_DEST_SERIAL;  // Default to both UART0 and USB Serial output
static vprintf_like_t s_original_vprintf = NULL;

/**
 * THE ONLY ROUTER: Both ESP_LOGx and hasp_log_printf meet here.
 */
static int hasp_log_vprintf_router(const char *fmt, va_list args)
{
    if (!s_ready || s_destination == HASP_LOG_DEST_NONE) {
        return 0;
    }

    char buf[HASP_LOG_BUFFER_SIZE];

    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(buf, sizeof(buf), fmt, copy);
    va_end(copy);

    if (len <= 0) {
        return len;
    }

    buf[1] = '='; // test

    const int write_len =
        (len < (int)sizeof(buf)) ? len : (int)sizeof(buf) - 1;

    if (s_destination & HASP_LOG_DEST_UART0) {
        uart_write_bytes(HASP_LOG_UART_PORT, buf, write_len);
    }

    if (s_destination & HASP_LOG_DEST_USB) {
        usb_serial_jtag_write_bytes(
            (const uint8_t *)buf,
            write_len,
            pdMS_TO_TICKS(10));
    }

    return len;
}

/**
 * Public Variadic bridge wrapper for external strings (like LVGL).
 * This leverages esp_log_writev() to automatically build: I (1234) TAG: Message
 */
void hasp_log_printf_with_tag(esp_log_level_t level, const char* tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
    // This ESP-IDF core function generates the exact standard prefix format layout
    esp_log_writev(level, tag, fmt, args);
    
    va_end(args);
}

esp_err_t hasp_log_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    // Configure and Install Hardware UART0
    // const uart_config_t uart_config = {
    //     .baud_rate  = 115200,
    //     .data_bits  = UART_DATA_8_BITS,
    //     .parity     = UART_PARITY_DISABLE,
    //     .stop_bits  = UART_STOP_BITS_1,
    //     .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    //     .source_clk = UART_SCLK_DEFAULT,
    //     .rx_flow_ctrl_thresh = ,
    //     .flags =
    // };
    
    esp_err_t ret = uart_driver_install(HASP_LOG_UART_PORT, 256, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // ret = uart_param_config(HASP_LOG_UART_PORT, &uart_config);
    ret = uart_set_baudrate(HASP_LOG_UART_PORT, 115200)
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Fallback default pins used for board console headers on WT32-SC01 Plus
    uart_set_pin(HASP_LOG_UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Configure and Install Native USB Serial/JTAG Peripheral
    usb_serial_jtag_driver_config_t usb_config = {
        .tx_buffer_size = 256,
        .rx_buffer_size = 256,
    };
    
    ret = usb_serial_jtag_driver_install(&usb_config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) { // Allow fallback if already initialized by bootloader console
        ESP_LOGE(TAG, "Failed to install USB JTAG driver: %s", esp_err_to_name(ret));
        uart_driver_delete(HASP_LOG_UART_PORT);
        return ret;
    }

    // Hijack the main vprintf system pipeline
    s_original_vprintf = esp_log_set_vprintf(hasp_log_vprintf_router);
    
    s_ready = true;
    ESP_LOGI(TAG, "Dynamic logging router operational.");
    return ESP_OK;
}

bool hasp_log_ready(void)
{
    return s_ready;
}

hasp_log_dest_t hasp_log_get_destination(void)
{
    return s_destination;
}

void hasp_log_set_destination(hasp_log_dest_t dest)
{
    s_destination = dest;
}

esp_err_t hasp_log_deinit(void)
{
    if (!s_ready) {
        return ESP_OK;
    }

    // Restore standard printing system
    if (s_original_vprintf != NULL) {
        esp_log_set_vprintf(s_original_vprintf);
        s_original_vprintf = NULL;
    }

    uart_driver_delete(HASP_LOG_UART_PORT);
    usb_serial_jtag_driver_uninstall();

    s_ready = false;
    return ESP_OK;
}
