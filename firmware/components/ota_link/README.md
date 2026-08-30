# ota_link ESP-IDF Component

Reusable OTA link updater component for ESP32 projects.

## What It Provides

This component gives an ESP-IDF app a serial command channel for:

- checking whether the device responds
- reading firmware, chip, partition, rollback, heap, uptime, and boot info
- rebooting the device
- receiving a new app binary in chunks
- finalizing an OTA update into the next OTA partition
- aborting an in-progress OTA transfer
- marking the running app valid after health checks
- marking the running app invalid and rolling back

It currently ships with a UART transport and is useful for devices that are
physically connected over USB/UART, factory programming tools, lab hardware,
robotics projects, and products that want a simple service-port updater.

The packet format and OTA manager can be adapted to other transports such as
TCP over WiFi, TCP over Ethernet, RS485, USB CDC, Bluetooth SPP, BLE, or
CAN/TWAI. Small-packet transports need their own fragmentation layer or a
smaller maximum payload size.

## Use

Copy this directory into your ESP-IDF project:

```text
components/ota_link
```

Add it to your app component:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES ota_link
)
```

Start it from your app:

```c
#include "ota_link.h"

void app_main(void)
{
    ESP_ERROR_CHECK(ota_link_start());

    // Your application logic can run here.
}
```

The updater runs in its own FreeRTOS task and handles `PING`, `GET_INFO`,
`REBOOT`, `ROLLBACK`, `OTA_BEGIN`, `OTA_DATA`, `OTA_END`, and `OTA_ABORT`.

When ESP-IDF rollback is enabled, a newly booted OTA app must be marked valid by
the application after health checks pass. Until then, ESP-IDF can roll back to
the previous valid partition.

## Defaults

```text
UART:        UART_NUM_0
Baud:        115200
RX buffer:   2048 bytes
Task stack:  8192 bytes
Priority:    5
Rollback:    manual app confirmation
```

## Custom Config

```c
ota_link_config_t config = OTA_LINK_DEFAULT_CONFIG();
config.uart_port = UART_NUM_1;
config.baud_rate = 921600;
config.auto_mark_app_valid = true;
config.auto_mark_valid_delay_ms = 5000;

ESP_ERROR_CHECK(ota_link_start_with_config(&config));
```

## Custom Transport

The built-in transport is UART. To adapt the component to another link, provide
read/write callbacks:

```c
static int my_transport_read(void *context, uint8_t *buffer, size_t length, uint32_t timeout_ms)
{
    return my_link_read(context, buffer, length, timeout_ms);
}

static esp_err_t my_transport_write(void *context, const uint8_t *data, size_t length)
{
    return my_link_write(context, data, length);
}

static const ota_link_transport_t my_transport = {
    .read = my_transport_read,
    .write = my_transport_write,
};

void app_main(void)
{
    ota_link_config_t config = OTA_LINK_DEFAULT_CONFIG();
    config.transport = &my_transport;
    config.transport_context = my_link_handle;

    ESP_ERROR_CHECK(ota_link_start_with_config(&config));
}
```

For TCP over WiFi/Ethernet, the callbacks can wrap socket `recv()` and `send()`.
For BLE or CAN/TWAI, add fragmentation below these callbacks because the
default protocol payload can be up to 1024 bytes.

Your project must use an OTA-capable partition table with `otadata`, `ota_0`,
and at least one OTA app partition. `GET_INFO` discovers OTA app partitions
dynamically, so `ota_2` and additional slots are reported automatically.

## Rollback API

Enable ESP-IDF rollback in `sdkconfig.defaults`:

```text
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

Manual confirmation pattern:

```c
ota_link_config_t config = OTA_LINK_DEFAULT_CONFIG();
config.auto_mark_app_valid = false;
ESP_ERROR_CHECK(ota_link_start_with_config(&config));

if (app_health_checks_passed()) {
    ESP_ERROR_CHECK(ota_link_mark_app_valid());
} else {
    ESP_ERROR_CHECK(ota_link_mark_app_invalid_and_reboot());
}
```

`app_health_checks_passed()` should be your product's own startup decision.
Common checks include required peripherals, saved configuration, sensor or motor
startup, storage mounts, network readiness, or any task that must be alive for
the firmware to be considered safe.

Only call `ota_link_mark_app_valid()` after those checks pass. To reject the
running app and reboot into the previous valid firmware:

```c
ESP_ERROR_CHECK(ota_link_mark_app_invalid_and_reboot());
```
