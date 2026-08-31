#include "auth.h"

#include <string.h>
#include "esp_random.h"
#include "sha/sha_core.h"

#define AUTH_PROOF_PREFIX "auth-proof"
#define AUTH_SESSION_PREFIX "session"
#define HMAC_BLOCK_SIZE 64
#define SHA256_SIZE 32

typedef struct {
    bool required;
    bool pending;
    bool authenticated;
    uint8_t key[AUTH_KEY_MAX_SIZE];
    size_t key_len;
    uint8_t client_nonce[AUTH_NONCE_SIZE];
    uint8_t server_nonce[AUTH_NONCE_SIZE];
    uint8_t session_key[SHA256_SIZE];
} auth_state_t;

static auth_state_t s_auth;

static bool hmac_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *data,
    size_t data_len,
    uint8_t output[SHA256_SIZE]
)
{
    uint8_t key_block[HMAC_BLOCK_SIZE] = {0};
    uint8_t inner_input[HMAC_BLOCK_SIZE + PROTOCOL_MAX_PAYLOAD + 3];
    uint8_t inner_hash[SHA256_SIZE];
    uint8_t outer_input[HMAC_BLOCK_SIZE + SHA256_SIZE];

    if (key_len > HMAC_BLOCK_SIZE) {
        esp_sha(SHA2_256, key, key_len, key_block);
    } else if (key != NULL && key_len > 0) {
        memcpy(key_block, key, key_len);
    }

    for (size_t i = 0; i < HMAC_BLOCK_SIZE; ++i) {
        inner_input[i] = key_block[i] ^ 0x36;
        outer_input[i] = key_block[i] ^ 0x5C;
    }
    if (data != NULL && data_len > 0) {
        memcpy(&inner_input[HMAC_BLOCK_SIZE], data, data_len);
    }

    esp_sha(SHA2_256, inner_input, HMAC_BLOCK_SIZE + data_len, inner_hash);
    memcpy(&outer_input[HMAC_BLOCK_SIZE], inner_hash, SHA256_SIZE);
    esp_sha(SHA2_256, outer_input, sizeof(outer_input), output);
    return true;
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t length)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < length; ++i) {
        diff |= left[i] ^ right[i];
    }
    return diff == 0;
}

static bool hmac_prefixed_nonce_pair(
    const char *prefix,
    const uint8_t *key,
    size_t key_len,
    const uint8_t client_nonce[AUTH_NONCE_SIZE],
    const uint8_t server_nonce[AUTH_NONCE_SIZE],
    uint8_t output[SHA256_SIZE]
)
{
    uint8_t input[sizeof(AUTH_PROOF_PREFIX) + (2 * AUTH_NONCE_SIZE)];
    size_t prefix_len = strlen(prefix);
    if (prefix_len + (2 * AUTH_NONCE_SIZE) > sizeof(input)) {
        return false;
    }

    memcpy(input, prefix, prefix_len);
    memcpy(&input[prefix_len], client_nonce, AUTH_NONCE_SIZE);
    memcpy(&input[prefix_len + AUTH_NONCE_SIZE], server_nonce, AUTH_NONCE_SIZE);
    return hmac_sha256(key, key_len, input, prefix_len + (2 * AUTH_NONCE_SIZE), output);
}

static bool compute_packet_tag(
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t length,
    uint8_t output[SHA256_SIZE]
)
{
    uint8_t input[1 + 2 + PROTOCOL_MAX_PAYLOAD];
    input[0] = command;
    input[1] = sequence & 0xFF;
    input[2] = (sequence >> 8) & 0xFF;
    if (length > 0 && payload != NULL) {
        memcpy(&input[3], payload, length);
    }

    return hmac_sha256(s_auth.session_key, sizeof(s_auth.session_key), input, 3 + length, output);
}

void auth_configure(const uint8_t *key, size_t key_len, bool require_authentication)
{
    memset(&s_auth, 0, sizeof(s_auth));
    if (key == NULL || key_len == 0 || key_len > AUTH_KEY_MAX_SIZE) {
        return;
    }

    memcpy(s_auth.key, key, key_len);
    s_auth.key_len = key_len;
    s_auth.required = require_authentication;
}

bool auth_is_enabled(void)
{
    return s_auth.required;
}

bool auth_command_is_protected(uint8_t command)
{
    return command == CMD_REBOOT ||
           command == CMD_ROLLBACK ||
           command == CMD_OTA_BEGIN ||
           command == CMD_OTA_DATA ||
           command == CMD_OTA_END ||
           command == CMD_OTA_ABORT;
}

protocol_error_t auth_handle_packet(const protocol_packet_t *packet)
{
    if (!s_auth.required) {
        return ERR_UNKNOWN_COMMAND;
    }

    if (packet->length == AUTH_NONCE_SIZE) {
        memcpy(s_auth.client_nonce, packet->payload, AUTH_NONCE_SIZE);
        esp_fill_random(s_auth.server_nonce, AUTH_NONCE_SIZE);
        if (!hmac_prefixed_nonce_pair(
                AUTH_SESSION_PREFIX,
                s_auth.key,
                s_auth.key_len,
                s_auth.client_nonce,
                s_auth.server_nonce,
                s_auth.session_key)) {
            return ERR_AUTH_REQUIRED;
        }

        s_auth.pending = true;
        s_auth.authenticated = false;
        uint8_t response[1 + AUTH_NONCE_SIZE];
        response[0] = ERR_OK;
        memcpy(&response[1], s_auth.server_nonce, AUTH_NONCE_SIZE);
        protocol_send_packet(CMD_ACK, packet->sequence, response, sizeof(response));
        return ERR_OK;
    }

    if (packet->length == AUTH_NONCE_SIZE + AUTH_TAG_SIZE) {
        uint8_t expected[SHA256_SIZE];
        if (!s_auth.pending ||
            !constant_time_equal(packet->payload, s_auth.client_nonce, AUTH_NONCE_SIZE) ||
            !hmac_prefixed_nonce_pair(
                AUTH_PROOF_PREFIX,
                s_auth.key,
                s_auth.key_len,
                s_auth.client_nonce,
                s_auth.server_nonce,
                expected) ||
            !constant_time_equal(&packet->payload[AUTH_NONCE_SIZE], expected, AUTH_TAG_SIZE)) {
            return ERR_AUTH_REQUIRED;
        }

        s_auth.pending = false;
        s_auth.authenticated = true;
        protocol_send_ack(packet->sequence);
        return ERR_OK;
    }

    return ERR_INVALID_LENGTH;
}

protocol_error_t auth_unwrap_packet(
    const protocol_packet_t *packet,
    const uint8_t **payload,
    uint16_t *length
)
{
    *payload = packet->payload;
    *length = packet->length;

    if (!s_auth.required || !auth_command_is_protected(packet->command)) {
        return ERR_OK;
    }
    if (!s_auth.authenticated || packet->length < AUTH_TAG_SIZE) {
        return ERR_AUTH_REQUIRED;
    }

    uint16_t unwrapped_length = packet->length - AUTH_TAG_SIZE;
    uint8_t expected[SHA256_SIZE];
    if (!compute_packet_tag(packet->command, packet->sequence, packet->payload, unwrapped_length, expected) ||
        !constant_time_equal(&packet->payload[unwrapped_length], expected, AUTH_TAG_SIZE)) {
        return ERR_AUTH_REQUIRED;
    }

    *length = unwrapped_length;
    return ERR_OK;
}
