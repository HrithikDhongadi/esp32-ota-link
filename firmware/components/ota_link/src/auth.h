#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "protocol.h"

#define AUTH_KEY_MAX_SIZE 64
#define AUTH_NONCE_SIZE 16
#define AUTH_TAG_SIZE 16

void auth_configure(const uint8_t *key, size_t key_len, bool require_authentication);
bool auth_is_enabled(void);
bool auth_command_is_protected(uint8_t command);
protocol_error_t auth_handle_packet(const protocol_packet_t *packet);
protocol_error_t auth_unwrap_packet(
    const protocol_packet_t *packet,
    const uint8_t **payload,
    uint16_t *length
);
