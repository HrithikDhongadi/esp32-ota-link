#include "serial_ota.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "esp32_serial_ota";

void app_main(void)
{
    ESP_ERROR_CHECK(serial_ota_start());
    ESP_LOGI(TAG, "example app started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
