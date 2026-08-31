#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/uart.h"
#include "esp_err.h"

#define PROTOCOL_MAGIC_0 0xAA
#define PROTOCOL_MAGIC_1 0x55
#define PROTOCOL_VERSION 1
#define PROTOCOL_MAX_PAYLOAD 1024

/**
 * @brief Wire command identifiers for protocol version 1.
 */
typedef enum {
    CMD_PING = 0x01,
    CMD_GET_INFO = 0x02,
    CMD_REBOOT = 0x03,
    CMD_ROLLBACK = 0x04,
    CMD_AUTH = 0x05,
    CMD_OTA_BEGIN = 0x10,
    CMD_OTA_DATA = 0x11,
    CMD_OTA_END = 0x12,
    CMD_OTA_ABORT = 0x13,
    CMD_ACK = 0xF0,
    CMD_NACK = 0xF1,
    CMD_INFO = 0xF2,
} protocol_command_t;

/**
 * @brief Error values returned in NACK payloads.
 */
typedef enum {
    ERR_OK = 0x00,
    ERR_UNKNOWN_COMMAND = 0x01,
    ERR_INVALID_PACKET = 0x02,
    ERR_CRC_ERROR = 0x03,
    ERR_INVALID_LENGTH = 0x04,
    ERR_BUSY = 0x05,
    ERR_AUTH_REQUIRED = 0x06,
    ERR_OTA_ALREADY_STARTED = 0x10,
    ERR_OTA_NOT_STARTED = 0x11,
    ERR_OTA_INVALID_OFFSET = 0x12,
    ERR_OTA_FLASH_ERROR = 0x13,
    ERR_OTA_HASH_MISMATCH = 0x14,
    ERR_OTA_INVALID_IMAGE = 0x15,
    ERR_OTA_SIZE_ERROR = 0x16,
    ERR_ROLLBACK_NOT_AVAILABLE = 0x17,
} protocol_error_t;

/**
 * @brief Decoded protocol packet.
 */
typedef struct {
    uint8_t version;
    uint8_t command;
    uint16_t sequence;
    uint16_t length;
    uint8_t payload[PROTOCOL_MAX_PAYLOAD];
} protocol_packet_t;

/**
 * @brief Streaming parser state.
 *
 * The parser accepts one byte at a time, ignores leading noise, validates
 * CRC32, and returns true only when a full packet is decoded.
 */
typedef struct {
    uint8_t buffer[2 + 1 + 1 + 2 + 2 + PROTOCOL_MAX_PAYLOAD + 4];
    size_t length;
} protocol_parser_t;

/** @brief Reset parser state before feeding bytes. */
void protocol_parser_init(protocol_parser_t *parser);

/** @brief Feed one byte into the parser. */
bool protocol_parser_feed(protocol_parser_t *parser, uint8_t byte, protocol_packet_t *packet);

/** @brief Use the built-in UART writer for protocol responses. */
void protocol_set_uart_port(uart_port_t uart_port);

/** @brief Install the active writer used for protocol responses. */
void protocol_set_writer(esp_err_t (*write)(void *context, const uint8_t *data, size_t length), void *context);

/** @brief Encode and send one protocol packet. */
void protocol_send_packet(uint8_t command, uint16_t sequence, const uint8_t *payload, uint16_t length);

/** @brief Send ACK with ERR_OK status. */
void protocol_send_ack(uint16_t sequence);

/** @brief Send NACK with a protocol_error_t code. */
void protocol_send_nack(uint16_t sequence, protocol_error_t error);
