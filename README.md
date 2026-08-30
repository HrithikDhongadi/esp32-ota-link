# esp32-serial-ota

Binary serial device-management and OTA update tooling for ESP32-WROOM-32.

The project has two pieces:

- `firmware/`: ESP-IDF firmware for the ESP32.
- `host/`: Python CLI that talks to the ESP32 over USB serial.

The current firmware supports a custom binary packet protocol with `PING`,
`GET_INFO`, `REBOOT`, `OTA_BEGIN`, `OTA_DATA`, `OTA_END`, and `OTA_ABORT`.
The OTA firmware side is packaged as a reusable ESP-IDF component.

## Current Status

Working on real hardware:

- ESP32-WROOM-32 target using ESP-IDF.
- Binary packet framing over USB serial.
- CRC32 packet validation.
- Host-side streaming packet parser.
- `ping` command.
- `info` command.
- `reboot` command.
- `OTA_BEGIN` metadata handshake.
- `OTA_DATA` staged firmware chunk transfer.
- `OTA_END` image validation and boot-partition selection.
- `OTA_ABORT` safe cancellation.
- Persistent boot counter in NVS.
- Interactive host shell that keeps the serial port open.

Not implemented yet:

- automatic reboot/reconnect after update
- OTA_DATA retry logic
- Rollback handling
- Firmware signing/security

## Project Layout

```text
firmware/
  components/
    serial_ota/        Reusable ESP-IDF serial OTA component
  main/                Small example app using the component
  partitions.csv       OTA-capable partition table
  sdkconfig.defaults   Required default ESP-IDF settings
host/                  Python host CLI and protocol implementation
protocol/              Wire protocol documentation
tests/                 Host-side protocol tests
plan.md                Original full project roadmap
```

## Reusing The Component

Copy this directory into any ESP-IDF project:

```text
firmware/components/serial_ota
```

Then add the component to your app's `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES serial_ota
)
```

Start the updater from your app:

```c
#include "serial_ota.h"

void app_main(void)
{
    ESP_ERROR_CHECK(serial_ota_start());

    // Your blink, motor, sensor, or product logic can run here.
}
```

By default, `serial_ota_start()` uses:

```text
UART:        UART_NUM_0
Baud:        115200
RX buffer:   2048 bytes
Task stack:  8192 bytes
Priority:    5
```

For custom UART settings:

```c
serial_ota_config_t config = SERIAL_OTA_DEFAULT_CONFIG();
config.uart_port = UART_NUM_1;
config.baud_rate = 921600;

ESP_ERROR_CHECK(serial_ota_start_with_config(&config));
```

Every firmware that should remain updateable must include this component or an
equivalent updater. If you OTA a plain blink app without the component, the app
will run, but `espctl update` will no longer be available until you flash again
with `idf.py`.

## Requirements

Firmware:

- ESP-IDF v6.1 or compatible
- ESP32-WROOM-32 board
- 4 MB flash configuration

Host:

- Python 3.10+
- `pyserial`
- `pytest` for tests

Install host dependencies:

```bash
python3 -m pip install -r host/requirements.txt
```

## Build And Flash

From the firmware directory:

```bash
cd firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

If your board appears as `/dev/ttyACM0`, use that port instead.

The project uses an OTA-capable partition table:

```text
nvs
otadata
phy_init
ota_0
ota_1
```

## Host Commands

From the repository root:

```bash
python3 -m host.espctl --port /dev/ttyUSB0 ping
python3 -m host.espctl --port /dev/ttyUSB0 info
python3 -m host.espctl --port /dev/ttyUSB0 reboot
python3 -m host.espctl --port /dev/ttyUSB0 update firmware/build/esp32_serial_ota.bin
python3 -m host.espctl --port /dev/ttyUSB0 abort
```

Recommended during development:

```bash
python3 -m host.espctl --port /dev/ttyUSB0 shell
```

Inside the shell:

```text
espctl> ping
espctl> info
espctl> reboot
espctl> update firmware/build/esp32_serial_ota.bin
espctl> abort
espctl> quit
```

Many ESP32 dev boards reset when the serial port opens. The shell mode keeps the
port open, so repeated commands do not reset the board.

The `update` command sends `OTA_BEGIN`, all `OTA_DATA` chunks, and `OTA_END`.
After a successful update, run `reboot` to boot the finalized image.

## Proven OTA Flow

This has been tested on hardware:

```text
Before update:
Firmware:          0.1.1
Running partition: ota_1

Update:
OTA begin accepted
Uploading:         100%
OTA data transferred
OTA finalized

After reboot:
Firmware:          0.1.2
Running partition: ota_0
```

## Example Info Output

```text
Project:           esp32_serial_ota
Firmware:          0.1.2
Protocol:          1
Chip:              ESP32
ESP-IDF:           v6.1
Running partition: ota_0
Free heap:         298144 bytes
Boot count:        8
Uptime:            17 seconds
Build date:        Aug 29 2026
```

`Boot count` is stored in NVS and increments once per ESP32 boot. It is useful
for confirming that `reboot` actually restarted the device.

## Test

Run host-side protocol tests:

```bash
python3 -m pytest
```

These tests cover packet building, streaming parse behavior, fragmented packets,
garbage before magic bytes, back-to-back packets, bad CRC rejection, and info
payload parsing.

## Protocol

Packet format:

```text
MAGIC AA55
VERSION u8
CMD u8
SEQ u16
LENGTH u16
PAYLOAD bytes
CRC32 u32
```

All multi-byte fields are little-endian. See `protocol/protocol.md` for the
full wire format.

## Next Milestone

Automate update completion:

```text
Host:
- reboot after finalized update
- wait for device reconnect
- verify running partition and version

ESP32:
- mark new app valid after startup checks
- support rollback for failed new firmware
```
