#include "ota_link.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "esp32_ota_link";

static void app_health_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(5000));

#if CONFIG_ESP32_OTA_LINK_EXAMPLE_FORCE_INVALID
    ESP_LOGW(TAG, "example health check failed; requesting rollback");
    ESP_ERROR_CHECK(ota_link_mark_app_invalid_and_reboot());
#else
    esp_err_t err = ota_link_mark_app_valid();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA app confirmed valid");
    } else if (err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "failed to confirm OTA app: %s", esp_err_to_name(err));
    }

    vTaskDelete(NULL);
#endif
}

void app_main(void)
{
    ota_link_config_t config = OTA_LINK_DEFAULT_CONFIG();
    config.auto_mark_app_valid = false;
#if CONFIG_ESP32_OTA_LINK_EXAMPLE_REQUIRE_AUTH
    static const uint8_t auth_key[] = CONFIG_ESP32_OTA_LINK_EXAMPLE_AUTH_KEY;
    config.auth_key = auth_key;
    config.auth_key_len = sizeof(auth_key) - 1;
    config.require_authentication = true;
#endif
    ESP_ERROR_CHECK(ota_link_start_with_config(&config));
    ESP_LOGI(TAG, "example app started");

    xTaskCreate(app_health_task, "app_health", 3072, NULL, tskIDLE_PRIORITY + 1, NULL);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
