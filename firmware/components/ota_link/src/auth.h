#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "protocol.h"

#define AUTH_KEY_MAX_SIZE 64
#define AUTH_NONCE_SIZE 16
#define AUTH_TAG_SIZE 16

/**
 * @brief Configure optional HMAC-SHA256 authentication for protected commands.
 *
 * The key is copied into component-owned state. Passing an invalid key disables
 * authentication state so ota_link_start_with_config() can reject bad
 * production configurations before commands are processed.
 *
 * @param key Pre-shared authentication key.
 * @param key_len Key length in bytes. Must be 1..AUTH_KEY_MAX_SIZE.
 * @param require_authentication True to require authenticated protected commands.
 */
void auth_configure(const uint8_t *key, size_t key_len, bool require_authentication);

/**
 * @brief Return whether protected commands currently require authentication.
 */
bool auth_is_enabled(void);

/**
 * @brief Return whether a command mutates device state and is auth-protected.
 *
 * Protected commands include reboot, rollback, OTA begin/data/end, and OTA
 * abort. Discovery commands such as ping, info, and auth handshake packets stay
 * available before authentication.
 *
 * @param command Protocol command byte.
 */
bool auth_command_is_protected(uint8_t command);

/**
 * @brief Process an AUTH handshake packet.
 *
 * A 16-byte payload starts a challenge and returns the server nonce plus device
 * proof. A 32-byte payload completes the challenge using the client proof.
 * This function sends successful AUTH responses directly because they have
 * custom payload shapes.
 *
 * @param packet Decoded AUTH request packet.
 * @return ERR_OK if a response was sent, otherwise the NACK error to send.
 */
protocol_error_t auth_handle_packet(const protocol_packet_t *packet);

/**
 * @brief Replay-check an authenticated protected request before executing it.
 *
 * If the packet is an exact duplicate of a recently executed authenticated
 * request, the cached response is resent and @p replayed is set true. If the
 * sequence number is reused with different packet bytes, ERR_AUTH_REQUIRED is
 * returned so the caller can reject the request without executing it.
 *
 * @param packet Decoded request packet.
 * @param replayed Set true when a cached response was resent.
 * @return ERR_OK on fresh request or handled replay, otherwise auth error.
 */
protocol_error_t auth_check_replay(const protocol_packet_t *packet, bool *replayed);

/**
 * @brief Verify and strip the authentication tag from a protected request.
 *
 * For unprotected commands, this returns the original payload unchanged. For
 * protected commands, a valid session and trailing AUTH_TAG_SIZE-byte HMAC tag
 * are required.
 *
 * @param packet Decoded request packet.
 * @param payload Receives the command payload without the auth tag.
 * @param length Receives the payload length without the auth tag.
 * @return ERR_OK on success or ERR_AUTH_REQUIRED/ERR_INVALID_LENGTH on failure.
 */
protocol_error_t auth_unwrap_packet(
    const protocol_packet_t *packet,
    const uint8_t **payload,
    uint16_t *length
);

/**
 * @brief Send, authenticate, and cache a response for a protected request.
 *
 * When authentication is active for the request command, a response tag is
 * appended and the complete response is saved in the replay cache. Exact packet
 * retries can then receive the same response without re-executing the command.
 *
 * @param request Original request packet.
 * @param response_command Response command, usually CMD_ACK or CMD_NACK.
 * @param payload Response payload before any response auth tag.
 * @param length Response payload length before any response auth tag.
 */
void auth_send_response(
    const protocol_packet_t *request,
    uint8_t response_command,
    const uint8_t *payload,
    uint16_t length
);

/**
 * @brief Send an authenticated-aware ACK for a request.
 */
void auth_send_ack(const protocol_packet_t *request);

/**
 * @brief Send an authenticated-aware NACK for a request.
 */
void auth_send_nack(const protocol_packet_t *request, protocol_error_t error);
