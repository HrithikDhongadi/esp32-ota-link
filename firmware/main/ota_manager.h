#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

#define OTA_SHA256_SIZE 32

protocol_error_t ota_manager_begin(const uint8_t *payload, uint16_t length);
protocol_error_t ota_manager_write_data(const uint8_t *payload, uint16_t length);
protocol_error_t ota_manager_end(void);
protocol_error_t ota_manager_abort(void);
bool ota_manager_is_active(void);
