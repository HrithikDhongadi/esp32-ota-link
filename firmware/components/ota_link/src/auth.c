#include "auth.h"

#include <string.h>
#include "esp_random.h"
#include "sha/sha_core.h"

#define AUTH_PROOF_PREFIX "auth-proof"
#define AUTH_DEVICE_PROOF_PREFIX "device-proof"
#define AUTH_SESSION_PREFIX "session"
#define AUTH_RESPONSE_PREFIX "response"
#define HMAC_BLOCK_SIZE 64
#define SHA256_SIZE 32
#define AUTH_REPLAY_CACHE_SIZE 8
#define HMAC_MAX_DATA_SIZE ((sizeof(AUTH_RESPONSE_PREFIX) - 1) + 4 + PROTOCOL_MAX_PAYLOAD)

/**
 * @brief Cached authenticated response for retry-safe command handling.
 *
 * The cache key is the request command, sequence number, and a SHA-256 digest
 * over the exact authenticated request bytes. This lets the host retry a lost
 * ACK by resending the same packet while preventing an attacker from reusing a
 * sequence number with different authenticated payload bytes.
 */
typedef struct {
    bool valid;
    uint8_t request_command;
    uint16_t sequence;
    uint8_t request_digest[SHA256_SIZE];
    uint8_t response_command;
    uint16_t response_length;
    uint8_t response_payload[PROTOCOL_MAX_PAYLOAD];
} auth_replay_entry_t;

/**
 * @brief In-memory authentication session state.
 *
 * Sessions are intentionally volatile. A device reboot clears the session and
 * forces the host to authenticate again, which keeps reboot/rollback recovery
 * simple and avoids persisting link-auth material in NVS.
 */
typedef struct {
    bool required;
    bool pending;
    bool authenticated;
    uint8_t key[AUTH_KEY_MAX_SIZE];
    size_t key_len;
    uint8_t client_nonce[AUTH_NONCE_SIZE];
    uint8_t server_nonce[AUTH_NONCE_SIZE];
    uint8_t session_key[SHA256_SIZE];
    auth_replay_entry_t replay_cache[AUTH_REPLAY_CACHE_SIZE];
    size_t next_replay_entry;
} auth_state_t;

static auth_state_t s_auth;

/**
 * @brief Small HMAC-SHA256 helper built on ESP-IDF's SHA primitive.
 *
 * ESP-IDF v6.1 does not expose the mbedtls_md_hmac() symbol consistently for
 * this component build, so the component implements the standard HMAC
 * construction locally. The maximum input size is bounded by protocol packet
 * size plus the largest domain-separation prefix used below.
 */
static bool hmac_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *data,
    size_t data_len,
    uint8_t output[SHA256_SIZE]
)
{
    uint8_t key_block[HMAC_BLOCK_SIZE] = {0};
    uint8_t inner_input[HMAC_BLOCK_SIZE + HMAC_MAX_DATA_SIZE];
    uint8_t inner_hash[SHA256_SIZE];
    uint8_t outer_input[HMAC_BLOCK_SIZE + SHA256_SIZE];

    if (key_len > HMAC_BLOCK_SIZE) {
        esp_sha(SHA2_256, key, key_len, key_block);
    } else if (key != NULL && key_len > 0) {
        memcpy(key_block, key, key_len);
    }
    if (data_len > HMAC_MAX_DATA_SIZE) {
        return false;
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
    /* The string prefix domain-separates challenge proof, device proof, and
     * session-key derivation even though all three use the same nonce pair.
     */
    uint8_t input[32 + (2 * AUTH_NONCE_SIZE)];
    size_t prefix_len = strlen(prefix);
    if (prefix_len + (2 * AUTH_NONCE_SIZE) > sizeof(input)) {
        return false;
    }

    memcpy(input, prefix, prefix_len);
    memcpy(&input[prefix_len], client_nonce, AUTH_NONCE_SIZE);
    memcpy(&input[prefix_len + AUTH_NONCE_SIZE], server_nonce, AUTH_NONCE_SIZE);
    return hmac_sha256(key, key_len, input, prefix_len + (2 * AUTH_NONCE_SIZE), output);
}

static bool compute_request_digest(
    const protocol_packet_t *packet,
    uint8_t output[SHA256_SIZE]
)
{
    /* Hash the exact authenticated request shape used on the wire, excluding
     * only the outer framing/CRC. The digest is for replay-cache identity, not
     * for authenticity; authenticity is still enforced by HMAC verification.
     */
    uint8_t input[1 + 2 + PROTOCOL_MAX_PAYLOAD];
    input[0] = packet->command;
    input[1] = packet->sequence & 0xFF;
    input[2] = (packet->sequence >> 8) & 0xFF;
    if (packet->length > 0) {
        memcpy(&input[3], packet->payload, packet->length);
    }

    esp_sha(SHA2_256, input, 3 + packet->length, output);
    return true;
}

static bool compute_packet_tag(
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t length,
    uint8_t output[SHA256_SIZE]
)
{
    /* Request tags bind the command and sequence to the payload so a valid tag
     * cannot be moved to another command or replayed under another sequence.
     */
    uint8_t input[1 + 2 + PROTOCOL_MAX_PAYLOAD];
    input[0] = command;
    input[1] = sequence & 0xFF;
    input[2] = (sequence >> 8) & 0xFF;
    if (length > 0 && payload != NULL) {
        memcpy(&input[3], payload, length);
    }

    return hmac_sha256(s_auth.session_key, sizeof(s_auth.session_key), input, 3 + length, output);
}

static bool compute_response_tag(
    uint8_t request_command,
    uint8_t response_command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t length,
    uint8_t output[SHA256_SIZE]
)
{
    /* Response tags also bind the original request command. Without that, a
     * signed ACK for one protected command could be replayed as the response to
     * another command with the same sequence.
     */
    uint8_t input[sizeof(AUTH_RESPONSE_PREFIX) - 1 + 1 + 1 + 2 + PROTOCOL_MAX_PAYLOAD];
    size_t offset = 0;
    memcpy(input, AUTH_RESPONSE_PREFIX, sizeof(AUTH_RESPONSE_PREFIX) - 1);
    offset += sizeof(AUTH_RESPONSE_PREFIX) - 1;
    input[offset++] = request_command;
    input[offset++] = response_command;
    input[offset++] = sequence & 0xFF;
    input[offset++] = (sequence >> 8) & 0xFF;
    if (length > 0 && payload != NULL) {
        memcpy(&input[offset], payload, length);
        offset += length;
    }

    return hmac_sha256(s_auth.session_key, sizeof(s_auth.session_key), input, offset, output);
}

static void clear_replay_cache(void)
{
    memset(s_auth.replay_cache, 0, sizeof(s_auth.replay_cache));
    s_auth.next_replay_entry = 0;
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
        /* Start a fresh challenge. Any old authenticated replay cache belongs
         * to the previous session and must not survive into the new one.
         */
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
        clear_replay_cache();
        uint8_t device_proof[SHA256_SIZE];
        if (!hmac_prefixed_nonce_pair(
                AUTH_DEVICE_PROOF_PREFIX,
                s_auth.key,
                s_auth.key_len,
                s_auth.client_nonce,
                s_auth.server_nonce,
                device_proof)) {
            return ERR_AUTH_REQUIRED;
        }

        uint8_t response[1 + AUTH_NONCE_SIZE + AUTH_TAG_SIZE];
        response[0] = ERR_OK;
        memcpy(&response[1], s_auth.server_nonce, AUTH_NONCE_SIZE);
        memcpy(&response[1 + AUTH_NONCE_SIZE], device_proof, AUTH_TAG_SIZE);
        protocol_send_packet(CMD_ACK, packet->sequence, response, sizeof(response));
        return ERR_OK;
    }

    if (packet->length == AUTH_NONCE_SIZE + AUTH_TAG_SIZE) {
        /* If the AUTH proof ACK was lost, the host may retry the same proof
         * after the device has already marked the session authenticated. Accept
         * that exact proof and return ACK again instead of forcing a new
         * challenge.
         */
        uint8_t expected[SHA256_SIZE];
        if ((!s_auth.pending && !s_auth.authenticated) ||
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

protocol_error_t auth_check_replay(const protocol_packet_t *packet, bool *replayed)
{
    *replayed = false;
    if (!s_auth.required || !auth_command_is_protected(packet->command)) {
        return ERR_OK;
    }
    if (!s_auth.authenticated) {
        return ERR_AUTH_REQUIRED;
    }

    uint8_t request_digest[SHA256_SIZE];
    compute_request_digest(packet, request_digest);
    for (size_t i = 0; i < AUTH_REPLAY_CACHE_SIZE; ++i) {
        auth_replay_entry_t *entry = &s_auth.replay_cache[i];
        if (!entry->valid ||
            entry->request_command != packet->command ||
            entry->sequence != packet->sequence) {
            continue;
        }

        if (!constant_time_equal(entry->request_digest, request_digest, SHA256_SIZE)) {
            /* Same command and sequence but different bytes is never a
             * legitimate retry. Treat it as an auth failure and do not execute.
             */
            return ERR_AUTH_REQUIRED;
        }

        /* Exact duplicate: resend the previous response and leave device state
         * untouched. This is what makes lost ACK recovery safe for commands
         * such as OTA_BEGIN, OTA_END, OTA_ABORT, REBOOT, and ROLLBACK.
         */
        protocol_send_packet(
            entry->response_command,
            packet->sequence,
            entry->response_payload,
            entry->response_length
        );
        *replayed = true;
        return ERR_OK;
    }

    return ERR_OK;
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

void auth_send_response(
    const protocol_packet_t *request,
    uint8_t response_command,
    const uint8_t *payload,
    uint16_t length
)
{
    uint8_t response_payload[PROTOCOL_MAX_PAYLOAD];
    uint16_t response_length = length;
    if (length > 0 && payload != NULL) {
        memcpy(response_payload, payload, length);
    }

    if (s_auth.required &&
        s_auth.authenticated &&
        auth_command_is_protected(request->command)) {
        if (length + AUTH_TAG_SIZE > PROTOCOL_MAX_PAYLOAD) {
            protocol_send_nack(request->sequence, ERR_INVALID_LENGTH);
            return;
        }

        uint8_t tag[SHA256_SIZE];
        compute_response_tag(
            request->command,
            response_command,
            request->sequence,
            payload,
            length,
            tag
        );
        memcpy(&response_payload[length], tag, AUTH_TAG_SIZE);
        response_length = length + AUTH_TAG_SIZE;

        uint8_t request_digest[SHA256_SIZE];
        compute_request_digest(request, request_digest);
        /* Ring-buffer eviction is acceptable because the host only retries the
         * current command. Old entries are retained just long enough to reject
         * short-window replays without unbounded memory growth.
         */
        auth_replay_entry_t *entry = &s_auth.replay_cache[s_auth.next_replay_entry];
        entry->valid = true;
        entry->request_command = request->command;
        entry->sequence = request->sequence;
        memcpy(entry->request_digest, request_digest, SHA256_SIZE);
        entry->response_command = response_command;
        entry->response_length = response_length;
        memcpy(entry->response_payload, response_payload, response_length);
        s_auth.next_replay_entry = (s_auth.next_replay_entry + 1) % AUTH_REPLAY_CACHE_SIZE;
    }

    protocol_send_packet(response_command, request->sequence, response_payload, response_length);
}

void auth_send_ack(const protocol_packet_t *request)
{
    uint8_t status = ERR_OK;
    auth_send_response(request, CMD_ACK, &status, 1);
}

void auth_send_nack(const protocol_packet_t *request, protocol_error_t error)
{
    uint8_t payload = error;
    auth_send_response(request, CMD_NACK, &payload, 1);
}
