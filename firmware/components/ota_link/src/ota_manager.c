#include "ota_manager.h"

#include <inttypes.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#define OTA_BEGIN_PAYLOAD_SIZE (4 + OTA_SHA256_SIZE)
#define OTA_DATA_HEADER_SIZE 8

typedef struct {
    bool active;
    uint32_t firmware_size;
    uint32_t bytes_received;
    uint8_t expected_sha256[OTA_SHA256_SIZE];
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
} ota_state_t;

static const char *TAG = "ota_manager";
static ota_state_t s_ota;
static uint8_t s_actual_sha256[OTA_SHA256_SIZE];

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

protocol_error_t ota_manager_begin(const uint8_t *payload, uint16_t length)
{
    if (s_ota.active) {
        return ERR_OTA_ALREADY_STARTED;
    }
    if (length != OTA_BEGIN_PAYLOAD_SIZE) {
        return ERR_INVALID_LENGTH;
    }

    uint32_t firmware_size = read_u32_le(payload);
    if (firmware_size == 0) {
        return ERR_OTA_SIZE_ERROR;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        ESP_LOGE(TAG, "no OTA update partition found");
        return ERR_OTA_INVALID_IMAGE;
    }
    if (firmware_size > partition->size) {
        ESP_LOGE(TAG, "firmware size %" PRIu32 " exceeds partition size %lu",
                 firmware_size, (unsigned long)partition->size);
        return ERR_OTA_SIZE_ERROR;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(partition, firmware_size, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return ERR_OTA_FLASH_ERROR;
    }

    memset(&s_ota, 0, sizeof(s_ota));
    s_ota.active = true;
    s_ota.firmware_size = firmware_size;
    s_ota.handle = handle;
    s_ota.partition = partition;
    memcpy(s_ota.expected_sha256, &payload[4], OTA_SHA256_SIZE);

    ESP_LOGI(TAG, "OTA begin: partition=%s size=%" PRIu32,
             partition->label, firmware_size);
    return ERR_OK;
}

protocol_error_t ota_manager_abort(void)
{
    if (!s_ota.active) {
        return ERR_OTA_NOT_STARTED;
    }

    esp_err_t err = esp_ota_abort(s_ota.handle);
    memset(&s_ota, 0, sizeof(s_ota));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_abort failed: %s", esp_err_to_name(err));
        return ERR_OTA_FLASH_ERROR;
    }

    ESP_LOGI(TAG, "OTA aborted");
    return ERR_OK;
}

protocol_error_t ota_manager_write_data(const uint8_t *payload, uint16_t length)
{
    if (!s_ota.active) {
        return ERR_OTA_NOT_STARTED;
    }
    if (length <= OTA_DATA_HEADER_SIZE) {
        return ERR_INVALID_LENGTH;
    }

    uint32_t chunk_number = read_u32_le(payload);
    uint32_t offset = read_u32_le(&payload[4]);
    const uint8_t *data = &payload[OTA_DATA_HEADER_SIZE];
    uint16_t data_length = length - OTA_DATA_HEADER_SIZE;

    if (offset != s_ota.bytes_received) {
        ESP_LOGW(TAG, "invalid OTA offset: got=%" PRIu32 " expected=%" PRIu32,
                 offset, s_ota.bytes_received);
        return ERR_OTA_INVALID_OFFSET;
    }
    if (offset + data_length > s_ota.firmware_size) {
        ESP_LOGW(TAG, "OTA chunk exceeds firmware size: offset=%" PRIu32
                 " len=%u size=%" PRIu32,
                 offset, data_length, s_ota.firmware_size);
        return ERR_OTA_SIZE_ERROR;
    }

    esp_err_t err = esp_ota_write(s_ota.handle, data, data_length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        return ERR_OTA_FLASH_ERROR;
    }

    s_ota.bytes_received += data_length;
    if ((chunk_number % 64) == 0 || s_ota.bytes_received == s_ota.firmware_size) {
        ESP_LOGI(TAG, "OTA data: chunk=%" PRIu32 " received=%" PRIu32 "/%" PRIu32,
                 chunk_number, s_ota.bytes_received, s_ota.firmware_size);
    }
    return ERR_OK;
}

protocol_error_t ota_manager_end(void)
{
    if (!s_ota.active) {
        return ERR_OTA_NOT_STARTED;
    }
    if (s_ota.bytes_received != s_ota.firmware_size) {
        ESP_LOGW(TAG, "OTA incomplete: received=%" PRIu32 " expected=%" PRIu32,
                 s_ota.bytes_received, s_ota.firmware_size);
        return ERR_OTA_SIZE_ERROR;
    }

    esp_err_t err = esp_ota_end(s_ota.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        memset(&s_ota, 0, sizeof(s_ota));
        return ERR_OTA_INVALID_IMAGE;
    }

    err = esp_partition_get_sha256(s_ota.partition, s_actual_sha256);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_get_sha256 failed: %s", esp_err_to_name(err));
        memset(&s_ota, 0, sizeof(s_ota));
        return ERR_OTA_HASH_MISMATCH;
    }
    if (memcmp(s_actual_sha256, s_ota.expected_sha256, OTA_SHA256_SIZE) != 0) {
        ESP_LOGE(TAG, "OTA SHA-256 mismatch");
        memset(&s_ota, 0, sizeof(s_ota));
        return ERR_OTA_HASH_MISMATCH;
    }

    err = esp_ota_set_boot_partition(s_ota.partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        memset(&s_ota, 0, sizeof(s_ota));
        return ERR_OTA_INVALID_IMAGE;
    }

    ESP_LOGI(TAG, "OTA finalized: next boot partition=%s", s_ota.partition->label);
    memset(&s_ota, 0, sizeof(s_ota));
    return ERR_OK;
}

bool ota_manager_is_active(void)
{
    return s_ota.active;
}
