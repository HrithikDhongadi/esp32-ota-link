#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/uart.h"

#define PROTOCOL_MAGIC_0 0xAA
#define PROTOCOL_MAGIC_1 0x55
#define PROTOCOL_VERSION 1
#define PROTOCOL_MAX_PAYLOAD 1024

typedef enum {
    CMD_PING = 0x01,
    CMD_GET_INFO = 0x02,
    CMD_REBOOT = 0x03,
    CMD_OTA_BEGIN = 0x10,
    CMD_OTA_DATA = 0x11,
    CMD_OTA_END = 0x12,
    CMD_OTA_ABORT = 0x13,
    CMD_ACK = 0xF0,
    CMD_NACK = 0xF1,
    CMD_INFO = 0xF2,
} protocol_command_t;

typedef enum {
    ERR_OK = 0x00,
    ERR_UNKNOWN_COMMAND = 0x01,
    ERR_INVALID_PACKET = 0x02,
    ERR_CRC_ERROR = 0x03,
    ERR_INVALID_LENGTH = 0x04,
    ERR_BUSY = 0x05,
    ERR_OTA_ALREADY_STARTED = 0x10,
    ERR_OTA_NOT_STARTED = 0x11,
    ERR_OTA_INVALID_OFFSET = 0x12,
    ERR_OTA_FLASH_ERROR = 0x13,
    ERR_OTA_HASH_MISMATCH = 0x14,
    ERR_OTA_INVALID_IMAGE = 0x15,
    ERR_OTA_SIZE_ERROR = 0x16,
} protocol_error_t;

typedef struct {
    uint8_t version;
    uint8_t command;
    uint16_t sequence;
    uint16_t length;
    uint8_t payload[PROTOCOL_MAX_PAYLOAD];
} protocol_packet_t;

typedef struct {
    uint8_t buffer[2 + 1 + 1 + 2 + 2 + PROTOCOL_MAX_PAYLOAD + 4];
    size_t length;
} protocol_parser_t;

void protocol_parser_init(protocol_parser_t *parser);
bool protocol_parser_feed(protocol_parser_t *parser, uint8_t byte, protocol_packet_t *packet);
void protocol_set_uart_port(uart_port_t uart_port);
void protocol_send_packet(uint8_t command, uint16_t sequence, const uint8_t *payload, uint16_t length);
void protocol_send_ack(uint16_t sequence);
void protocol_send_nack(uint16_t sequence, protocol_error_t error);
