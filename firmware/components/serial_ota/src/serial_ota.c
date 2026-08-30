#include "serial_ota.h"

#include "commands.h"
#include "device_info.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "protocol.h"

static const char *TAG = "serial_ota";
static protocol_parser_t s_parser;
static protocol_packet_t s_packet;
static serial_ota_config_t s_config;
static TaskHandle_t s_task_handle;

static void serial_ota_task(void *arg)
{
    (void)arg;
    protocol_parser_init(&s_parser);
    ESP_LOGI(TAG, "command loop started on UART%d", s_config.uart_port);

    while (true) {
        uint8_t byte = 0;
        int read = uart_read_bytes(s_config.uart_port, &byte, 1, pdMS_TO_TICKS(100));
        if (read == 1 && protocol_parser_feed(&s_parser, byte, &s_packet)) {
            commands_handle_packet(&s_packet);
        }
    }
}

esp_err_t serial_ota_start(void)
{
    serial_ota_config_t config = SERIAL_OTA_DEFAULT_CONFIG();
    return serial_ota_start_with_config(&config);
}

esp_err_t serial_ota_start_with_config(const serial_ota_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_config = *config;
    device_info_init();
    protocol_set_uart_port(s_config.uart_port);

    const uart_config_t uart_config = {
        .baud_rate = s_config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(
        s_config.uart_port,
        s_config.rx_buffer_size,
        0,
        0,
        NULL,
        0
    );
    if (err != ESP_OK) {
        return err;
    }

    err = uart_param_config(s_config.uart_port, &uart_config);
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t created = xTaskCreate(
        serial_ota_task,
        "serial_ota",
        s_config.task_stack_size,
        NULL,
        s_config.task_priority,
        &s_task_handle
    );
    if (created != pdPASS) {
        uart_driver_delete(s_config.uart_port);
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
