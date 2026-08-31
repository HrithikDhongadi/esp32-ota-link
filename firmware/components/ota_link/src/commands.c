#include "commands.h"

#include "auth.h"
#include "device_info.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_manager.h"
#include "ota_link.h"

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

    case CMD_AUTH: {
        protocol_error_t error = auth_handle_packet(packet);
        if (error != ERR_OK) {
            protocol_send_nack(packet->sequence, error);
        }
        break;
    }

    case CMD_REBOOT:
        {
        const uint8_t *payload = NULL;
        uint16_t length = 0;
        protocol_error_t error = auth_unwrap_packet(packet, &payload, &length);
        (void)payload;
        if (error != ERR_OK || length != 0) {
            protocol_send_nack(packet->sequence, error != ERR_OK ? error : ERR_INVALID_LENGTH);
            break;
        }
        if (ota_manager_is_active()) {
            protocol_send_nack(packet->sequence, ERR_BUSY);
            break;
        }
        protocol_send_ack(packet->sequence);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
        break;
        }

    case CMD_ROLLBACK:
        {
        const uint8_t *payload = NULL;
        uint16_t length = 0;
        protocol_error_t error = auth_unwrap_packet(packet, &payload, &length);
        (void)payload;
        if (error != ERR_OK || length != 0) {
            protocol_send_nack(packet->sequence, error != ERR_OK ? error : ERR_INVALID_LENGTH);
            break;
        }
        if (ota_manager_is_active()) {
            protocol_send_nack(packet->sequence, ERR_BUSY);
            break;
        }
        if (!esp_ota_check_rollback_is_possible()) {
            protocol_send_nack(packet->sequence, ERR_ROLLBACK_NOT_AVAILABLE);
            break;
        }
        protocol_send_ack(packet->sequence);
        vTaskDelay(pdMS_TO_TICKS(100));
        ota_link_mark_app_invalid_and_reboot();
        break;
        }

    case CMD_OTA_BEGIN: {
        const uint8_t *payload = NULL;
        uint16_t length = 0;
        protocol_error_t error = auth_unwrap_packet(packet, &payload, &length);
        if (error == ERR_OK) {
            error = ota_manager_begin(payload, length);
        }
        if (error == ERR_OK) {
            protocol_send_ack(packet->sequence);
        } else {
            protocol_send_nack(packet->sequence, error);
        }
        break;
    }

    case CMD_OTA_ABORT: {
        const uint8_t *payload = NULL;
        uint16_t length = 0;
        protocol_error_t error = auth_unwrap_packet(packet, &payload, &length);
        (void)payload;
        if (error == ERR_OK && length != 0) {
            error = ERR_INVALID_LENGTH;
        }
        if (error == ERR_OK) {
            error = ota_manager_abort();
        }
        if (error == ERR_OK) {
            protocol_send_ack(packet->sequence);
        } else {
            protocol_send_nack(packet->sequence, error);
        }
        break;
    }

    case CMD_OTA_DATA: {
        const uint8_t *payload = NULL;
        uint16_t length = 0;
        protocol_error_t error = auth_unwrap_packet(packet, &payload, &length);
        if (error == ERR_OK) {
            error = ota_manager_write_data(payload, length);
        }
        if (error == ERR_OK) {
            protocol_send_ack(packet->sequence);
        } else {
            protocol_send_nack(packet->sequence, error);
        }
        break;
    }

    case CMD_OTA_END: {
        const uint8_t *payload = NULL;
        uint16_t length = 0;
        protocol_error_t error = auth_unwrap_packet(packet, &payload, &length);
        (void)payload;
        if (error == ERR_OK && length != 0) {
            error = ERR_INVALID_LENGTH;
        }
        if (error == ERR_OK) {
            error = ota_manager_end();
        }
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
