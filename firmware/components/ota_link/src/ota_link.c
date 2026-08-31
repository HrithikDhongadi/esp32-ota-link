#include "ota_link.h"

#include "auth.h"
#include "commands.h"
#include "device_info.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/task.h"
#include "protocol.h"

static const char *TAG = "ota_link";
static protocol_parser_t s_parser;
static protocol_packet_t s_packet;
static ota_link_config_t s_config;
static TaskHandle_t s_task_handle;
static TaskHandle_t s_mark_valid_task_handle;

static int uart_transport_read(void *context, uint8_t *buffer, size_t length, uint32_t timeout_ms)
{
    uart_port_t uart_port = (uart_port_t)(intptr_t)context;
    return uart_read_bytes(uart_port, buffer, length, pdMS_TO_TICKS(timeout_ms));
}

static esp_err_t uart_transport_write(void *context, const uint8_t *data, size_t length)
{
    uart_port_t uart_port = (uart_port_t)(intptr_t)context;
    int written = uart_write_bytes(uart_port, data, length);
    return written == (int)length ? ESP_OK : ESP_FAIL;
}

static const ota_link_transport_t s_uart_transport = {
    .read = uart_transport_read,
    .write = uart_transport_write,
};

static const char *ota_state_name(esp_ota_img_states_t state)
{
    switch (state) {
    case ESP_OTA_IMG_NEW:
        return "NEW";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "PENDING_VERIFY";
    case ESP_OTA_IMG_VALID:
        return "VALID";
    case ESP_OTA_IMG_INVALID:
        return "INVALID";
    case ESP_OTA_IMG_ABORTED:
        return "ABORTED";
    case ESP_OTA_IMG_UNDEFINED:
        return "UNDEFINED";
    default:
        return "UNKNOWN";
    }
}

static bool running_app_needs_validation(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err != ESP_OK) {
        return false;
    }

    ESP_LOGI(TAG, "running OTA state: %s", ota_state_name(state));
    return state == ESP_OTA_IMG_NEW || state == ESP_OTA_IMG_PENDING_VERIFY;
}

static void mark_valid_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(s_config.auto_mark_valid_delay_ms));

    esp_err_t err = ota_link_mark_app_valid();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "auto mark valid failed: %s", esp_err_to_name(err));
    }

    s_mark_valid_task_handle = NULL;
    vTaskDelete(NULL);
}

static void ota_link_task(void *arg)
{
    (void)arg;
    protocol_parser_init(&s_parser);
    ESP_LOGI(TAG, "command loop started");

    while (true) {
        uint8_t byte = 0;
        int read = s_config.transport->read(s_config.transport_context, &byte, 1, 100);
        if (read == 1 && protocol_parser_feed(&s_parser, byte, &s_packet)) {
            commands_handle_packet(&s_packet);
        } else if (read < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

esp_err_t ota_link_start(void)
{
    ota_link_config_t config = OTA_LINK_DEFAULT_CONFIG();
    return ota_link_start_with_config(&config);
}

esp_err_t ota_link_start_with_config(const ota_link_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_config = *config;
    if (s_config.require_authentication &&
        (s_config.auth_key == NULL || s_config.auth_key_len == 0 || s_config.auth_key_len > AUTH_KEY_MAX_SIZE)) {
        return ESP_ERR_INVALID_ARG;
    }
    auth_configure(s_config.auth_key, s_config.auth_key_len, s_config.require_authentication);
    device_info_init();

    if (s_config.transport == NULL) {
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
            uart_driver_delete(s_config.uart_port);
            return err;
        }

        err = uart_set_pin(
            s_config.uart_port,
            s_config.tx_io_num,
            s_config.rx_io_num,
            s_config.rts_io_num,
            s_config.cts_io_num
        );
        if (err != ESP_OK) {
            uart_driver_delete(s_config.uart_port);
            return err;
        }

        s_config.transport = &s_uart_transport;
        s_config.transport_context = (void *)(intptr_t)s_config.uart_port;
        ESP_LOGI(TAG, "using UART%d at %d baud", s_config.uart_port, s_config.baud_rate);
        if (s_config.uart_port == UART_NUM_0) {
            ESP_LOGW(TAG, "UART0 often carries ESP-IDF console logs; use a dedicated UART or custom transport for production");
        }
    } else if (s_config.transport->read == NULL || s_config.transport->write == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    protocol_set_writer(s_config.transport->write, s_config.transport_context);

    BaseType_t created = xTaskCreate(
        ota_link_task,
        "ota_link",
        s_config.task_stack_size,
        NULL,
        s_config.task_priority,
        &s_task_handle
    );
    if (created != pdPASS) {
        if (config->transport == NULL) {
            uart_driver_delete(s_config.uart_port);
        }
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (s_config.auto_mark_app_valid && running_app_needs_validation()) {
        created = xTaskCreate(
            mark_valid_task,
            "ota_link_valid",
            3072,
            NULL,
            tskIDLE_PRIORITY + 1,
            &s_mark_valid_task_handle
        );
        if (created != pdPASS) {
            ESP_LOGW(TAG, "failed to create auto-mark-valid task");
        }
    }

    return ESP_OK;
}

esp_err_t ota_link_mark_app_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err != ESP_OK) {
        return err;
    }
    if (state != ESP_OTA_IMG_NEW && state != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_ERR_INVALID_STATE;
    }

    err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "running app marked valid");
    }
    return err;
}

esp_err_t ota_link_mark_app_invalid_and_reboot(void)
{
    ESP_LOGW(TAG, "marking running app invalid and requesting rollback");
    return esp_ota_mark_app_invalid_rollback_and_reboot();
}
