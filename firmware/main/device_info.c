#include "device_info.h"

#include <inttypes.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocol.h"

#define FW_MAJOR 0
#define FW_MINOR 1
#define FW_PATCH 2

static const char *TAG = "device_info";
static uint32_t s_boot_count = 0;

static void put_u32_le(uint8_t *buffer, size_t *offset, uint32_t value)
{
    buffer[(*offset)++] = value & 0xFF;
    buffer[(*offset)++] = (value >> 8) & 0xFF;
    buffer[(*offset)++] = (value >> 16) & 0xFF;
    buffer[(*offset)++] = (value >> 24) & 0xFF;
}

static bool put_str8(uint8_t *buffer, size_t capacity, size_t *offset, const char *value)
{
    size_t length = strlen(value);
    if (length > 255 || *offset + 1 + length > capacity) {
        return false;
    }
    buffer[(*offset)++] = (uint8_t)length;
    memcpy(&buffer[*offset], value, length);
    *offset += length;
    return true;
}

void device_info_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }

    nvs_handle_t nvs;
    err = nvs_open("esp32_ota", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    uint32_t boot_count = 0;
    err = nvs_get_u32(nvs, "boot_count", &boot_count);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "boot_count read failed: %s", esp_err_to_name(err));
    }

    s_boot_count = boot_count + 1;
    err = nvs_set_u32(nvs, "boot_count", s_boot_count);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "boot_count write failed: %s", esp_err_to_name(err));
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "boot count: %" PRIu32, s_boot_count);
}

size_t device_info_build_payload(uint8_t *buffer, size_t capacity)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *desc = esp_app_get_description();

    const char *chip_model = "ESP32";
    const char *partition = running ? running->label : "unknown";
    const char *idf_version = desc ? desc->idf_ver : "unknown";
    const char *project_name = desc ? desc->project_name : "esp32_serial_ota";
    const char *build_date = desc ? desc->date : "unknown";

    size_t offset = 0;
    if (capacity < 16) {
        return 0;
    }

    buffer[offset++] = PROTOCOL_VERSION;
    buffer[offset++] = FW_MAJOR;
    buffer[offset++] = FW_MINOR;
    buffer[offset++] = FW_PATCH;
    put_u32_le(buffer, &offset, (uint32_t)(esp_timer_get_time() / 1000000ULL));
    put_u32_le(buffer, &offset, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    put_u32_le(buffer, &offset, s_boot_count);

    if (!put_str8(buffer, capacity, &offset, chip_model) ||
        !put_str8(buffer, capacity, &offset, partition) ||
        !put_str8(buffer, capacity, &offset, idf_version) ||
        !put_str8(buffer, capacity, &offset, project_name) ||
        !put_str8(buffer, capacity, &offset, build_date)) {
        return 0;
    }
    return offset;
}
