#include "protocol.h"

#include <string.h>
#include "driver/uart.h"

#define UART_PORT UART_NUM_0
#define HEADER_SIZE 8
#define CRC_SIZE 4

static uint8_t s_tx_buffer[HEADER_SIZE + PROTOCOL_MAX_PAYLOAD + CRC_SIZE];

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_le16(uint8_t *data, uint16_t value)
{
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    data[2] = (value >> 16) & 0xFF;
    data[3] = (value >> 24) & 0xFF;
}

static uint32_t protocol_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = -(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void protocol_parser_init(protocol_parser_t *parser)
{
    parser->length = 0;
}

bool protocol_parser_feed(protocol_parser_t *parser, uint8_t byte, protocol_packet_t *packet)
{
    if (parser->length == 0 && byte != PROTOCOL_MAGIC_0) {
        return false;
    }

    if (parser->length == 1 && byte != PROTOCOL_MAGIC_1) {
        parser->length = (byte == PROTOCOL_MAGIC_0) ? 1 : 0;
        parser->buffer[0] = PROTOCOL_MAGIC_0;
        return false;
    }

    parser->buffer[parser->length++] = byte;

    if (parser->length < HEADER_SIZE) {
        return false;
    }

    uint16_t payload_length = read_le16(&parser->buffer[6]);
    if (payload_length > PROTOCOL_MAX_PAYLOAD) {
        parser->length = 0;
        return false;
    }

    size_t packet_length = HEADER_SIZE + payload_length + CRC_SIZE;
    if (parser->length < packet_length) {
        return false;
    }

    uint32_t expected_crc = read_le32(&parser->buffer[HEADER_SIZE + payload_length]);
    uint32_t actual_crc = protocol_crc32(&parser->buffer[2], HEADER_SIZE - 2 + payload_length);
    if (actual_crc != expected_crc) {
        parser->length = 0;
        return false;
    }

    packet->version = parser->buffer[2];
    packet->command = parser->buffer[3];
    packet->sequence = read_le16(&parser->buffer[4]);
    packet->length = payload_length;
    memcpy(packet->payload, &parser->buffer[HEADER_SIZE], payload_length);
    parser->length = 0;
    return true;
}

void protocol_send_packet(uint8_t command, uint16_t sequence, const uint8_t *payload, uint16_t length)
{
    s_tx_buffer[0] = PROTOCOL_MAGIC_0;
    s_tx_buffer[1] = PROTOCOL_MAGIC_1;
    s_tx_buffer[2] = PROTOCOL_VERSION;
    s_tx_buffer[3] = command;
    write_le16(&s_tx_buffer[4], sequence);
    write_le16(&s_tx_buffer[6], length);
    if (length > 0 && payload != NULL) {
        memcpy(&s_tx_buffer[HEADER_SIZE], payload, length);
    }

    uint32_t crc = protocol_crc32(&s_tx_buffer[2], HEADER_SIZE - 2 + length);
    write_le32(&s_tx_buffer[HEADER_SIZE + length], crc);
    uart_write_bytes(UART_PORT, s_tx_buffer, HEADER_SIZE + length + CRC_SIZE);
}

void protocol_send_ack(uint16_t sequence)
{
    uint8_t status = ERR_OK;
    protocol_send_packet(CMD_ACK, sequence, &status, 1);
}

void protocol_send_nack(uint16_t sequence, protocol_error_t error)
{
    uint8_t payload = error;
    protocol_send_packet(CMD_NACK, sequence, &payload, 1);
}
