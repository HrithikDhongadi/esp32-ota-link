#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transport callback table used by the OTA link component.
 *
 * The default configuration uses ESP-IDF UART APIs. Provide this structure when
 * adapting the updater to another byte stream or message link such as TCP,
 * USB CDC, RS485, BLE, or CAN/TWAI with an extra fragmentation layer.
 */
typedef struct {
    /**
     * @brief Read bytes from the transport.
     *
     * @param context User-provided transport context.
     * @param buffer Destination buffer.
     * @param length Maximum bytes to read.
     * @param timeout_ms Read timeout in milliseconds.
     * @return Number of bytes read, 0 on timeout, or a negative value on error.
     */
    int (*read)(void *context, uint8_t *buffer, size_t length, uint32_t timeout_ms);

    /**
     * @brief Write bytes to the transport.
     *
     * @param context User-provided transport context.
     * @param data Bytes to write.
     * @param length Number of bytes to write.
     * @return ESP_OK on success, or an ESP-IDF error code on failure.
     */
    esp_err_t (*write)(void *context, const uint8_t *data, size_t length);
} ota_link_transport_t;

/**
 * @brief Runtime configuration for the OTA command task.
 *
 * Leave @c transport as NULL to use the built-in UART transport. When a custom
 * transport is provided, UART settings are ignored and the component calls the
 * supplied read/write callbacks instead.
 */
typedef struct {
    uart_port_t uart_port;
    int baud_rate;
    int tx_io_num;
    int rx_io_num;
    int rts_io_num;
    int cts_io_num;
    int rx_buffer_size;
    uint32_t task_stack_size;
    UBaseType_t task_priority;
    bool auto_mark_app_valid;
    uint32_t auto_mark_valid_delay_ms;

    /**
     * @brief Optional pre-shared key for HMAC-SHA256 command authentication.
     *
     * When require_authentication is true, reboot, rollback, OTA update, and
     * abort commands require the host to authenticate with this key first.
     * The key is copied during startup and may be released after the call.
     */
    const uint8_t *auth_key;
    size_t auth_key_len;
    bool require_authentication;
    const ota_link_transport_t *transport;
    void *transport_context;
} ota_link_config_t;

/**
 * @brief Default UART-based component configuration.
 */
#define OTA_LINK_DEFAULT_CONFIG() \
    { \
        .uart_port = UART_NUM_0, \
        .baud_rate = 115200, \
        .tx_io_num = UART_PIN_NO_CHANGE, \
        .rx_io_num = UART_PIN_NO_CHANGE, \
        .rts_io_num = UART_PIN_NO_CHANGE, \
        .cts_io_num = UART_PIN_NO_CHANGE, \
        .rx_buffer_size = 2048, \
        .task_stack_size = 8192, \
        .task_priority = 5, \
        .auto_mark_app_valid = false, \
        .auto_mark_valid_delay_ms = 5000, \
        .auth_key = NULL, \
        .auth_key_len = 0, \
        .require_authentication = false, \
        .transport = NULL, \
        .transport_context = NULL, \
    }

/**
 * @brief Start the OTA command task using OTA_LINK_DEFAULT_CONFIG().
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already started, or
 * another ESP-IDF error code.
 */
esp_err_t ota_link_start(void);

/**
 * @brief Start the OTA command task with an explicit configuration.
 *
 * @param config Component configuration. Must remain valid only for the call;
 * values are copied internally.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid arguments,
 * ESP_ERR_INVALID_STATE if already started, or another ESP-IDF error code.
 */
esp_err_t ota_link_start_with_config(const ota_link_config_t *config);

/**
 * @brief Mark the currently running OTA app as valid.
 *
 * Call this after application health checks pass. With ESP-IDF rollback enabled,
 * a newly booted app remains rollback-capable until it is marked valid.
 *
 * @return ESP_OK if marked valid, ESP_ERR_INVALID_STATE if the running app does
 * not need validation, or another ESP-IDF error code.
 */
esp_err_t ota_link_mark_app_valid(void);

/**
 * @brief Mark the currently running OTA app invalid and reboot for rollback.
 *
 * This function does not return on success.
 *
 * @return ESP-IDF error code if rollback could not be started.
 */
esp_err_t ota_link_mark_app_invalid_and_reboot(void);

#ifdef __cplusplus
}
#endif
