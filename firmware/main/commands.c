#include "commands.h"

#include "device_info.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
        uint8_t payload[PROTOCOL_MAX_PAYLOAD];
        size_t length = device_info_build_payload(payload, sizeof(payload));
        if (length == 0) {
            protocol_send_nack(packet->sequence, ERR_INVALID_LENGTH);
            break;
        }
        protocol_send_packet(CMD_INFO, packet->sequence, payload, (uint16_t)length);
        break;
    }

    case CMD_REBOOT:
        protocol_send_ack(packet->sequence);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
        break;

    default:
        protocol_send_nack(packet->sequence, ERR_UNKNOWN_COMMAND);
        break;
    }
}
