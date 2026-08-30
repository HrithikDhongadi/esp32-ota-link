#pragma once

#include <stdint.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uart_port_t uart_port;
    int baud_rate;
    int rx_buffer_size;
    uint32_t task_stack_size;
    UBaseType_t task_priority;
} serial_ota_config_t;

#define SERIAL_OTA_DEFAULT_CONFIG() \
    { \
        .uart_port = UART_NUM_0, \
        .baud_rate = 115200, \
        .rx_buffer_size = 2048, \
        .task_stack_size = 8192, \
        .task_priority = 5, \
    }

esp_err_t serial_ota_start(void);
esp_err_t serial_ota_start_with_config(const serial_ota_config_t *config);

#ifdef __cplusplus
}
#endif
