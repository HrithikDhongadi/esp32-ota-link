#include "commands.h"

#include "device_info.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_manager.h"

static uint8_t s_info_payload[PROTOCOL_MAX_PAYLOAD];

void commands_handle_packet(const protocol_packet_t *packet)
{
    if (packet->version != PROTOCOL_VERSION) {
        protocol_send_nack(packet->sequence, ERR_INVALID_PACKET);
        return;
    }

    switch (packet->command) {
    case CMD_PING:
        protocol_send_ack(packet->sequence);
        break;

    case CMD_GET_INFO: {
        size_t length = device_info_build_payload(s_info_payload, sizeof(s_info_payload));
        if (length == 0) {
            protocol_send_nack(packet->sequence, ERR_INVALID_LENGTH);
            break;
        }
        protocol_send_packet(CMD_INFO, packet->sequence, s_info_payload, (uint16_t)length);
        break;
    }

    case CMD_REBOOT:
        if (ota_manager_is_active()) {
            protocol_send_nack(packet->sequence, ERR_BUSY);
            break;
        }
        protocol_send_ack(packet->sequence);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
        break;

    case CMD_OTA_BEGIN: {
        protocol_error_t error = ota_manager_begin(packet->payload, packet->length);
        if (error == ERR_OK) {
            protocol_send_ack(packet->sequence);
        } else {
            protocol_send_nack(packet->sequence, error);
        }
        break;
    }

    case CMD_OTA_ABORT: {
        protocol_error_t error = ota_manager_abort();
        if (error == ERR_OK) {
            protocol_send_ack(packet->sequence);
        } else {
            protocol_send_nack(packet->sequence, error);
        }
        break;
    }

    case CMD_OTA_DATA: {
        protocol_error_t error = ota_manager_write_data(packet->payload, packet->length);
        if (error == ERR_OK) {
            protocol_send_ack(packet->sequence);
        } else {
            protocol_send_nack(packet->sequence, error);
        }
        break;
    }

    case CMD_OTA_END: {
        protocol_error_t error = ota_manager_end();
        if (error == ERR_OK) {
            protocol_send_ack(packet->sequence);
        } else {
            protocol_send_nack(packet->sequence, error);
        }
        break;
    }

    default:
        protocol_send_nack(packet->sequence, ERR_UNKNOWN_COMMAND);
        break;
    }
}
