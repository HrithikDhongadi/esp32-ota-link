# serial_ota ESP-IDF Component

Reusable serial OTA updater component for ESP32 projects.

## Use

Copy this directory into your ESP-IDF project:

```text
components/serial_ota
```

Add it to your app component:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES serial_ota
)
```

Start it from your app:

```c
#include "serial_ota.h"

void app_main(void)
{
    ESP_ERROR_CHECK(serial_ota_start());

    // Your application logic can run here.
}
```

The updater runs in its own FreeRTOS task and handles `PING`, `GET_INFO`,
`REBOOT`, `OTA_BEGIN`, `OTA_DATA`, `OTA_END`, and `OTA_ABORT`.

## Defaults

```text
UART:        UART_NUM_0
Baud:        115200
RX buffer:   2048 bytes
Task stack:  8192 bytes
Priority:    5
```

## Custom Config

```c
serial_ota_config_t config = SERIAL_OTA_DEFAULT_CONFIG();
config.uart_port = UART_NUM_1;
config.baud_rate = 921600;

ESP_ERROR_CHECK(serial_ota_start_with_config(&config));
```

Your project must use an OTA-capable partition table with `otadata`, `ota_0`,
and `ota_1` partitions.
