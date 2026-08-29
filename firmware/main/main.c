#include "commands.h"
#include "device_info.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol.h"

#define UART_PORT UART_NUM_0
#define UART_RX_BUF_SIZE 2048

static const char *TAG = "esp32_serial_ota";

void app_main(void)
{
    device_info_init();

    const uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &config));

    protocol_parser_t parser;
    protocol_packet_t packet;
    protocol_parser_init(&parser);

    ESP_LOGI(TAG, "esp32-serial-ota command loop started");

    while (true) {
        uint8_t byte = 0;
        int read = uart_read_bytes(UART_PORT, &byte, 1, pdMS_TO_TICKS(100));
        if (read == 1 && protocol_parser_feed(&parser, byte, &packet)) {
            commands_handle_packet(&packet);
        }
    }
}
