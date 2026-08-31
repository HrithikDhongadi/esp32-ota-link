# esp32-ota-link

Reusable ESP-IDF OTA link updater for ESP32-family projects.

`esp32-ota-link` gives an ESP32 app a small binary command channel for device
management and firmware updates over a byte-stream transport. The repo includes
a reusable ESP-IDF component, an example firmware app, and a Python host tool
that can push `.bin` images over USB serial/UART.

The first transport is UART/USB serial, but the firmware component is structured
so the same protocol can be adapted to RS485, TCP over WiFi, TCP over Ethernet,
USB CDC, Bluetooth SPP, BLE, CAN/TWAI, or another transport with read/write
callbacks.

## Why This Exists

Not every ESP32 is connected directly to the cloud.

In robotics, lab hardware, industrial tools, and distributed embedded systems,
ESP32 boards often sit behind an edge controller. They may control motors,
sensors, actuators, grippers, lights, or small subsystems while a Raspberry Pi,
Jetson, PLC, industrial PC, or master MCU coordinates the full machine.

In that setup, the edge controller is the natural update authority. It already
talks to each ESP32 node over UART, RS485, USB, Ethernet, CAN-style links, or a
custom transport. `esp32-ota-link` is built for that topology: updating and
managing ESP32 nodes through the communication link they already have.

## Highlights

- Reusable ESP-IDF component: `firmware/components/ota_link`
- Python host CLI: `python3 -m host.otalink`
- Binary packet framing with CRC32
- Optional HMAC-SHA256 link authentication for mutating commands
- Commands: `ping`, `info`, `reboot`, `rollback`, `update`, `abort`
- Chunked OTA transfer using ESP-IDF OTA APIs
- Image SHA256 validation before boot partition switch
- ESP-IDF rollback support for failed firmware
- Manual or delayed app-valid confirmation
- Dynamic OTA slot reporting, including 3-slot partition tables
- Ready-made firmware profiles for ESP32, S2, S3, C3, C5, C6, H2, and P4

## What You Can Use This For

Use this project when you want to update ESP32 firmware through a command
protocol instead of reflashing with `idf.py flash` every time.

Good fits:

- development boards connected over USB serial
- factory and service tools
- RS485/UART service links
- lab devices, test jigs, robots, motor controllers, and sensor nodes
- products where WiFi OTA is not available yet
- firmware demos that show OTA, rollback, and partition state
- ESP-IDF apps that need a simple recovery/update channel

The updater can install any valid ESP-IDF app image built for the same chip,
flash layout, and hardware. To remain updateable after OTA, the new firmware
must also include this component or an equivalent updater.

Use link authentication, firmware signing, encryption, or a trusted transport
before exposing update ports to untrusted users or physical access.

## Project Layout

```text
firmware/
  components/
    ota_link/          Reusable ESP-IDF OTA link component
  main/                Example app using the component
  profiles/            Optional chip/flash partition and sdkconfig profiles
  partitions.csv       Default ESP32-WROOM-32 OTA partition table
  sdkconfig.defaults   Default ESP-IDF settings
host/                  Python host CLI and protocol implementation
protocol/              Wire protocol documentation
tests/                 Host-side protocol tests
plan.md                Original project roadmap
```

## Requirements

Firmware:

- ESP-IDF v6.1 or compatible
- ESP32-family target supported by ESP-IDF
- OTA-capable partition table
- flash-size setting that matches the actual module

Host:

- Python 3.10+
- `pyserial`
- `pytest` for tests

Install host dependencies:

```bash
python3 -m pip install -r host/requirements.txt
```

## Quick Start

Build and flash the default ESP32-WROOM-32 example:

```bash
cd firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

Open the host shell from the repository root:

```bash
python3 -m host.otalink --port /dev/ttyUSB0 shell
```

Try the basic commands:

```text
otalink> ping
otalink> info
otalink> update firmware/build/esp32_ota_link.bin
otalink> reboot
```

If your board appears as `/dev/ttyACM0`, use that port instead.

Many ESP32 dev boards reset when the serial port opens. Shell mode keeps the
port open, so repeated commands do not reset the board between requests. This
matters after OTA: a newly booted image may spend a few seconds in
`PENDING_VERIFY` before the application marks itself valid.

## Host Commands

From the repository root:

```bash
python3 -m host.otalink --port /dev/ttyUSB0 ping
python3 -m host.otalink --port /dev/ttyUSB0 info
python3 -m host.otalink --port /dev/ttyUSB0 reboot
python3 -m host.otalink --port /dev/ttyUSB0 rollback
python3 -m host.otalink --port /dev/ttyUSB0 update firmware/build/esp32_ota_link.bin
python3 -m host.otalink --port /dev/ttyUSB0 abort
python3 -m host.otalink --port /dev/ttyUSB0 shell
```

If the device requires link authentication, pass the same pre-shared key to the
host tool. Text keys and `hex:` encoded keys are accepted:

```bash
python3 -m host.otalink --port /dev/ttyUSB0 --auth-key 'service-secret' info
python3 -m host.otalink --port /dev/ttyUSB0 --auth-key hex:736572766963652d736563726574 reboot
```

Inside `shell` mode:

```text
otalink> ping
otalink> info
otalink> reboot
otalink> rollback
otalink> update firmware/build/esp32_ota_link.bin
otalink> abort
otalink> quit
```

The `update` command sends `OTA_BEGIN`, all `OTA_DATA` chunks, and `OTA_END`.
After a successful update, run `reboot` to boot the finalized image.

In single-command terminal mode, wait before reopening the serial port for
`info`, because opening USB serial may reset some ESP32 dev boards:

```bash
python3 -m host.otalink --port /dev/ttyUSB0 update firmware/build/esp32_ota_link.bin
python3 -m host.otalink --port /dev/ttyUSB0 reboot
sleep 8
python3 -m host.otalink --port /dev/ttyUSB0 info
```

Shell mode avoids the extra serial-open reset:

```text
otalink> update firmware/build/esp32_ota_link.bin
otalink> reboot
otalink> info
```

If `info` is run immediately after reboot, `OTA state: PENDING_VERIFY` is
normal. After the example health task marks the app valid, it changes to
`OTA state: VALID`.

The `rollback` command marks the running app invalid and reboots into the
previous valid OTA partition when ESP-IDF rollback is available.

## Supported Firmware Profiles

The default project files target the board this repo was developed on:
ESP32-WROOM-32 with 4 MB flash and two OTA slots.

Optional profiles live in `firmware/profiles/`:

| Board / Module Family | Profile | Target | Flash | PSRAM | OTA Slots |
| --- | --- | --- | --- | --- | --- |
| ESP32 DevKitC / DevKitM / WROOM | `esp32_4mb_2ota` | `esp32` | 4 MB | none | 2 |
| ESP32 DevKitC / DevKitM / WROOM | `esp32_8mb_2ota` | `esp32` | 8 MB | board-specific | 2 |
| ESP32 DevKitC / DevKitM / WROVER | `esp32_wrover_4mb_2ota` | `esp32` | 4 MB | QSPI PSRAM | 2 |
| ESP32-S2-DevKit / Saola / WROOM | `esp32s2_4mb_2ota` | `esp32s2` | 4 MB | none | 2 |
| ESP32-S3-DevKitC / DevKitM / WROOM-1-N16R8 | `esp32s3_16mb_2ota` | `esp32s3` | 16 MB | 8 MB OPI | 2 |
| ESP32-S3-DevKitC / DevKitM / WROOM-1-N16R8 | `esp32s3_16mb_3ota` | `esp32s3` | 16 MB | 8 MB OPI | 3 |
| ESP32-C3-DevKitM-1 | `esp32c3_4mb_2ota` | `esp32c3` | 4 MB | none | 2 |
| ESP32-C5 DevKit | `esp32c5_4mb_2ota` | `esp32c5` | 4 MB | none | 2 |
| ESP32-C6-DevKitC | `esp32c6_4mb_2ota` | `esp32c6` | 4 MB | none | 2 |
| ESP32-H2 DevKit | `esp32h2_4mb_2ota` | `esp32h2` | 4 MB | none | 2 |
| ESP32-P4 DevKit / module | `esp32p4_16mb_2ota` | `esp32p4` | 16 MB | board-specific | 2 |

Example profile build:

```bash
cd firmware
idf.py \
  -B build_esp32s3_16mb_3ota \
  -D SDKCONFIG=build_esp32s3_16mb_3ota/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32s3_16mb_3ota.defaults" \
  set-target esp32s3 build
```

When flashing a profile build, pass the same `-B` build directory:

```bash
idf.py -B build_esp32s3_16mb_3ota flash
```

Running plain `idf.py flash` uses the default `firmware/build` directory. If
that directory was configured for another chip, ESP-IDF may rebuild or flash the
wrong target.

Use the firmware image from the same build directory for OTA updates:

```text
otalink> update firmware/build_esp32s3_16mb_3ota/esp32_ota_link.bin
```

See [Firmware Profiles Readme](firmware/profiles/README.md) for the full board matrix and build commands.
You can copy any profile and adjust flash size, storage partitions, OTA slot
count, or app slot size for your own board.

## Reusing The Component

Copy this directory into any ESP-IDF project:

```text
firmware/components/ota_link
```

Then add the component to your app's `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES ota_link
)
```

Start the updater from your app:

```c
#include "ota_link.h"

void app_main(void)
{
    ESP_ERROR_CHECK(ota_link_start());

    // Your blink, motor, sensor, or product logic can run here.
}
```

By default, `ota_link_start()` uses:

```text
UART:        UART_NUM_0
Baud:        115200
Pins:        ESP-IDF defaults for the selected UART
RX buffer:   2048 bytes
Task stack:  8192 bytes
Priority:    5
Rollback:    manual app confirmation
```

For custom UART settings:

```c
ota_link_config_t config = OTA_LINK_DEFAULT_CONFIG();
config.uart_port = UART_NUM_1;
config.tx_io_num = 17;
config.rx_io_num = 18;
config.baud_rate = 921600;
config.auto_mark_app_valid = true;
config.auto_mark_valid_delay_ms = 5000;

ESP_ERROR_CHECK(ota_link_start_with_config(&config));
```

When using a UART other than the board's default console UART, set
`tx_io_num` and `rx_io_num` explicitly for your board wiring.

For production hardware, prefer a dedicated UART or custom transport instead of
sharing UART0 with ESP-IDF console logs.

To require HMAC-SHA256 authentication before reboot, rollback, OTA update, or
abort commands:

```c
static const uint8_t ota_auth_key[] = "service-secret";

ota_link_config_t config = OTA_LINK_DEFAULT_CONFIG();
config.auth_key = ota_auth_key;
config.auth_key_len = sizeof(ota_auth_key) - 1;
config.require_authentication = true;

ESP_ERROR_CHECK(ota_link_start_with_config(&config));
```

The example app can also enable this through menuconfig:

```bash
cd firmware
idf.py -B build_esp32c3_4mb_2ota menuconfig
```

Then enable `esp32_ota_link example -> Require OTA link authentication in the
example app`, set the demo key, rebuild, and use the host with `--auth-key`.

## Current API Surface

The firmware side already exposes a reusable ESP-IDF C API through
`firmware/components/ota_link/include/ota_link.h`.

The host side currently provides a Python CLI and reusable protocol/client
building blocks, but it is not yet packaged as a full host SDK.

```text
MCU C API:       available
Host Python CLI: available
Host Python API: partial/internal
Host C API:      planned
Host C++ API:    planned
```

## Custom Transports

The component exposes `ota_link_transport_t` for links other than UART:

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

## OTA And Rollback

Every firmware that should remain updateable must include this component or an
equivalent updater. If you OTA a plain blink app without the component, the app
will run, but `otalink update` will no longer be available until you flash again
with `idf.py`.

The default partition table is OTA-capable:

```text
nvs
otadata
phy_init
ota_0
ota_1
```

`otalink info` discovers OTA app partitions dynamically. If your partition table
has `ota_2` or more OTA slots, the host tool reports those slots automatically.

By default, the reusable component does not confirm new OTA images. Call
`ota_link_mark_app_valid()` only after your app confirms that critical startup
work succeeded. If startup crashes or the app never marks itself valid, ESP-IDF
can roll back to the previous valid OTA partition.

In your own application, the pattern should look like this:

```c
#include "ota_link.h"

static bool app_health_checks_passed(void)
{
    /*
     * Replace these with checks that prove this firmware is actually usable:
     * - required peripherals initialized
     * - sensor/motor/display startup succeeded
     * - saved configuration loaded
     * - network or service task started if your product requires it
     */
    return true;
}

void app_main(void)
{
    ESP_ERROR_CHECK(ota_link_start());

    if (app_health_checks_passed()) {
        ESP_ERROR_CHECK(ota_link_mark_app_valid());
    } else {
        ESP_ERROR_CHECK(ota_link_mark_app_invalid_and_reboot());
    }

    /* Continue normal application work here. */
}
```

Only call `ota_link_mark_app_valid()` when the new firmware is safe to keep.
If checks fail, call `ota_link_mark_app_invalid_and_reboot()` to reject the
running image and boot back into the previous valid OTA slot.

To build an image that intentionally fails its example health check:

```bash
cd firmware
idf.py -B build_invalid -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.invalid.defaults" build
```

After OTA and reboot, the invalid demo waits about 5 seconds, marks itself
invalid, and ESP-IDF boots back into the previous valid partition. `info` then
shows the failed slot as `INVALID` under `OTA slots:`.

Keep the invalid demo in its own build directory. If
`CONFIG_ESP32_OTA_LINK_EXAMPLE_FORCE_INVALID=y` is enabled in your normal
`sdkconfig`, the normal `firmware/build/esp32_ota_link.bin` image will also
mark itself invalid and roll back.

## Example Info Output

```text
Project:           esp32_ota_link
Firmware:          0.1.3
Protocol:          1
Chip:              ESP32
ESP-IDF:           v6.1
Running partition: ota_0
Boot partition:    ota_0
OTA state:         VALID
OTA slots:
  ota_0: VALID
  ota_1: VALID
Rollback possible: yes
Free heap:         280800 bytes
Boot count:        38
Uptime:            23 seconds
Build date:        Aug 30 2026
```

`Boot count` is stored in NVS and increments once per ESP32 boot. It is useful
for confirming that `reboot` actually restarted the device.

## Proven Hardware Flow

This has been tested on real ESP32-WROOM-32 hardware:

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
Firmware:          0.1.3
Running partition: ota_0
```

Rollback was also tested with an intentionally invalid firmware image:

```text
ota_0: INVALID
ota_1: VALID
Rollback possible: no
```

The ESP32-C3 `esp32c3_4mb_2ota` profile has also been tested on real hardware:

```text
Hardware tests:
ping
info
interrupted OTA + abort
full OTA + reboot + app validation

Manual power-loss test:
power removed at 33% upload
running partition stayed on the previous valid slot
half-written target slot did not become bootable
```

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

## Test

Run host-side protocol tests:

```bash
python3 -m pytest
```

These tests cover packet building, streaming parse behavior, fragmented packets,
garbage before magic bytes, back-to-back packets, bad CRC rejection, info
payload parsing, OTA payload helpers, host retry behavior, and firmware metadata
handling. Hardware tests cover ping, info, interrupted update abort, full OTA
transfer, reboot, and app validation.

With hardware connected, set the serial port to run the opt-in ESP32 tests:

```bash
OTALINK_PORT=/dev/ttyUSB0 python3 -m pytest -m hardware
```

To include a full OTA transfer in the hardware test run, also set the firmware
image path:

```bash
OTALINK_PORT=/dev/ttyUSB0 \
OTALINK_FIRMWARE=firmware/build_esp32c3_4mb_2ota/esp32_ota_link.bin \
python3 -m pytest -m hardware
```

For an authenticated device, pass `OTALINK_AUTH_KEY` to the hardware tests:

```bash
OTALINK_PORT=/dev/ttyUSB0 \
OTALINK_AUTH_KEY=service-secret \
OTALINK_FIRMWARE=firmware/build_esp32c3_4mb_2ota/esp32_ota_link.bin \
python3 -m pytest -m hardware
```

## Roadmap

Completed:

- NVS init no longer erases application configuration
- OTA_DATA duplicate retry handling for lost ACKs
- firmware version is read from ESP-IDF app metadata
- configurable UART TX/RX/RTS/CTS pins
- UART0 console-log risk documented and warned at runtime
- optional HMAC-SHA256 link authentication
- host tests for protocol, retry, auth, and metadata
- ESP32-C3 hardware tests for ping, info, interrupted OTA abort, full OTA,
  reboot, and validation
- manual power-loss test during OTA upload

Remaining:

- automatic reboot/reconnect after update
- firmware-side command unit tests
- power-failure recovery tests
- richer application health-check hooks
- firmware signature verification
- full host Python API
- full host C API
- full host C++ API
