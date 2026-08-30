#include "device_info.h"

#include <inttypes.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocol.h"

#define FW_MAJOR 0
#define FW_MINOR 1
#define FW_PATCH 3
#define OTA_SUBTYPE_MIN ESP_PARTITION_SUBTYPE_APP_OTA_0
#define OTA_SUBTYPE_MAX ESP_PARTITION_SUBTYPE_APP_OTA_15

static const char *TAG = "device_info";
static uint32_t s_boot_count = 0;

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

static const char *chip_model_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:
        return "ESP32";
    case CHIP_ESP32S2:
        return "ESP32-S2";
    case CHIP_ESP32S3:
        return "ESP32-S3";
    case CHIP_ESP32C2:
        return "ESP32-C2";
    case CHIP_ESP32C3:
        return "ESP32-C3";
    case CHIP_ESP32C5:
        return "ESP32-C5";
    case CHIP_ESP32C6:
        return "ESP32-C6";
    case CHIP_ESP32C61:
        return "ESP32-C61";
    case CHIP_ESP32H2:
        return "ESP32-H2";
    case CHIP_ESP32H21:
        return "ESP32-H21";
    case CHIP_ESP32H4:
        return "ESP32-H4";
    case CHIP_ESP32P4:
        return "ESP32-P4";
    case CHIP_ESP32S31:
        return "ESP32-S31";
    case CHIP_POSIX_LINUX:
        return "POSIX-Linux";
    default:
        return "unknown";
    }
}

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

static bool partition_is_ota_slot(const esp_partition_t *partition)
{
    return partition != NULL &&
        partition->type == ESP_PARTITION_TYPE_APP &&
        partition->subtype >= OTA_SUBTYPE_MIN &&
        partition->subtype <= OTA_SUBTYPE_MAX;
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
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_app_desc_t *desc = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    if (running != NULL) {
        (void)esp_ota_get_state_partition(running, &ota_state);
    }

    const char *chip_model = chip_model_name(chip.model);
    const char *partition = running ? running->label : "unknown";
    const char *boot_partition = boot ? boot->label : "unknown";
    const char *running_ota_state = ota_state_name(ota_state);
    const char *idf_version = desc ? desc->idf_ver : "unknown";
    const char *project_name = desc ? desc->project_name : "esp32_ota_link";
    const char *build_date = desc ? desc->date : "unknown";

    size_t offset = 0;
    if (capacity < 17) {
        return 0;
    }

    buffer[offset++] = PROTOCOL_VERSION;
    buffer[offset++] = FW_MAJOR;
    buffer[offset++] = FW_MINOR;
    buffer[offset++] = FW_PATCH;
    put_u32_le(buffer, &offset, (uint32_t)(esp_timer_get_time() / 1000000ULL));
    put_u32_le(buffer, &offset, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    put_u32_le(buffer, &offset, s_boot_count);
    buffer[offset++] = esp_ota_check_rollback_is_possible() ? 1 : 0;

    if (!put_str8(buffer, capacity, &offset, chip_model) ||
        !put_str8(buffer, capacity, &offset, partition) ||
        !put_str8(buffer, capacity, &offset, boot_partition) ||
        !put_str8(buffer, capacity, &offset, running_ota_state) ||
        !put_str8(buffer, capacity, &offset, idf_version) ||
        !put_str8(buffer, capacity, &offset, project_name) ||
        !put_str8(buffer, capacity, &offset, build_date)) {
        return 0;
    }

    if (offset + 1 > capacity) {
        return 0;
    }
    size_t slot_count_offset = offset++;
    uint8_t slot_count = 0;

    esp_partition_iterator_t iterator = esp_partition_find(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_ANY,
        NULL
    );

    while (iterator != NULL) {
        const esp_partition_t *slot = esp_partition_get(iterator);
        iterator = esp_partition_next(iterator);
        if (!partition_is_ota_slot(slot)) {
            continue;
        }

        if (slot_count == UINT8_MAX) {
            esp_partition_iterator_release(iterator);
            return 0;
        }

        esp_ota_img_states_t slot_state = ESP_OTA_IMG_UNDEFINED;
        (void)esp_ota_get_state_partition(slot, &slot_state);

        if (!put_str8(buffer, capacity, &offset, slot->label) ||
            !put_str8(buffer, capacity, &offset, ota_state_name(slot_state))) {
            esp_partition_iterator_release(iterator);
            return 0;
        }
        slot_count++;
    }
    esp_partition_iterator_release(iterator);
    buffer[slot_count_offset] = slot_count;

    return offset;
}
