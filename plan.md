# ESP32 ↔ Linux Binary Protocol + Serial OTA Plan

## Goal

Build a small device-management system where a Linux PC communicates with an ESP32 over USB serial using a custom binary protocol.

The final system should support:

- Device discovery / ping
- Firmware version query
- Device information query
- Reboot command
- Reliable binary packet transport
- Firmware upload over serial
- OTA flashing to the inactive ESP32 partition
- Firmware SHA-256 verification
- Booting the new firmware
- Confirming the new firmware version after reboot
- Error handling and retries
- Optional firmware rollback
- Optional signed firmware verification

---

# 1. System Architecture

```text
┌───────────────────────┐
│ Linux PC              │
│                       │
│ espctl                │
│                       │
│ - ping                │
│ - info                │
│ - reboot              │
│ - update firmware.bin │
└───────────┬───────────┘
            │
            │ USB Serial
            │ Binary Protocol
            │
┌───────────▼───────────┐
│ ESP32                 │
│                       │
│ Protocol Parser       │
│ Command Handler       │
│ OTA Manager           │
│ Device Info Manager   │
└───────────────────────┘
```

Linux side:

- Start with Python
- Use `pyserial`
- Later optionally rewrite the tool in C/C++ or Rust

ESP32 side:

- Use ESP-IDF
- Use the ESP-IDF OTA APIs
- Use an OTA-capable partition table

---

# 2. Project Structure

Suggested repository layout:

```text
esp32-serial-ota/
│
├── firmware/
│   ├── CMakeLists.txt
│   ├── sdkconfig
│   ├── partitions.csv
│   │
│   └── main/
│       ├── main.c
│       ├── protocol.c
│       ├── protocol.h
│       ├── commands.c
│       ├── commands.h
│       ├── ota_manager.c
│       ├── ota_manager.h
│       ├── device_info.c
│       └── device_info.h
│
├── host/
│   ├── espctl.py
│   ├── protocol.py
│   ├── commands.py
│   └── requirements.txt
│
├── protocol/
│   └── protocol.md
│
├── tests/
│   ├── test_protocol.py
│   └── test_packets.py
│
└── plan.md
```

---

# 3. Protocol Design

Start with a simple framed binary protocol.

Suggested packet:

```text
┌────────┬─────────┬─────┬─────┬────────┬─────────┬────────┐
│ MAGIC  │ VERSION │ CMD │ SEQ │ LENGTH │ PAYLOAD │ CRC32  │
├────────┼─────────┼─────┼─────┼────────┼─────────┼────────┤
│ 2 byte │ 1 byte  │ 1 B │ 2 B │ 2 byte │ N bytes │ 4 byte │
└────────┴─────────┴─────┴─────┴────────┴─────────┴────────┘
```

Example magic value:

```text
AA 55
```

Initial protocol version:

```text
01
```

Suggested byte order:

```text
Little-endian
```

Maximum payload for the first version:

```text
1024 bytes
```

---

# 4. Initial Commands

```text
0x01  PING
0x02  GET_INFO
0x03  REBOOT

0x10  OTA_BEGIN
0x11  OTA_DATA
0x12  OTA_END
0x13  OTA_ABORT

0xF0  ACK
0xF1  NACK
```

Possible later commands:

```text
0x20  GET_LOG
0x21  GET_RESET_REASON
0x22  GET_PARTITIONS
0x23  FACTORY_RESET
0x24  SET_CONFIG
0x25  GET_CONFIG
```

---

# 5. Phase 1 — Establish Serial Communication

## ESP32

Configure USB serial or UART.

For development:

```text
Baud: 115200
Data: 8 bits
Parity: none
Stop: 1
```

Confirm Linux detects the board as something similar to:

```text
/dev/ttyUSB0
```

or:

```text
/dev/ttyACM0
```

## Linux

Install pyserial:

```bash
python3 -m pip install pyserial
```

Create a simple test program that:

1. Opens the serial device
2. Sends one byte
3. Receives one byte
4. Prints received bytes in hexadecimal

### Success criteria

```text
Linux can send and receive raw bytes without using text commands.
```

---

# 6. Phase 2 — Packet Encoder and Parser

Implement the packet format on both sides.

Linux:

```text
build_packet()
parse_packet()
crc32()
```

ESP32:

```text
protocol_feed_byte()
protocol_parse_packet()
protocol_send_packet()
```

The ESP32 parser should be a state machine.

Example states:

```text
WAIT_MAGIC_1
WAIT_MAGIC_2
READ_HEADER
READ_PAYLOAD
READ_CRC
VALIDATE
```

Do not assume that a complete packet arrives in one serial read.

### Success criteria

- Parser survives fragmented packets
- Parser ignores garbage before the magic bytes
- Invalid CRC packets are rejected
- Multiple packets can arrive back-to-back

---

# 7. Phase 3 — PING Command

Linux sends:

```text
PING
```

Binary command:

```text
CMD = 0x01
```

ESP32 responds with:

```text
ACK
```

or a dedicated PONG message later.

Example:

```text
PC                         ESP32

PING seq=1  ------------->

             <------------- ACK seq=1
```

### Success criteria

```bash
$ python3 espctl.py ping
Device responded
```

---

# 8. Phase 4 — GET_INFO

Create a binary device-info structure.

Suggested fields:

```text
protocol_version
firmware_major
firmware_minor
firmware_patch

chip_model
chip_revision

flash_size
free_heap

running_partition
reset_reason
uptime_seconds

project_name
idf_version
build_date
```

Avoid relying on null-terminated strings inside protocol packets.

For variable-size text fields use:

```text
[length][bytes]
```

Example:

```text
05 31 2E 32 2E 33
```

Meaning:

```text
length = 5
value  = "1.2.3"
```

The protocol is still binary even if some metadata fields contain UTF-8 text.

### Linux command

```bash
espctl info
```

Possible output:

```text
Device:            ESP32
Firmware:          1.0.0
Protocol:          1
ESP-IDF:           vX.X
Running partition: ota_0
Flash size:        4 MB
Free heap:         240 KB
Uptime:            483 seconds
```

### Success criteria

The Linux utility can query and display device metadata.

---

# 9. Phase 5 — Reboot Command

Implement:

```text
REBOOT = 0x03
```

Flow:

```text
PC                       ESP32

REBOOT seq=5 ---------->

           <------------- ACK seq=5

                         reboot
```

The host should expect the serial device to disappear temporarily.

### Success criteria

```bash
espctl reboot
```

reboots the device cleanly.

---

# 10. Phase 6 — OTA Partition Setup

Use an OTA-capable ESP-IDF partition table.

Example layout:

```text
nvs
otadata
ota_0
ota_1
```

Conceptually:

```text
┌─────────────────────┐
│ Bootloader          │
├─────────────────────┤
│ Partition table     │
├─────────────────────┤
│ NVS                 │
├─────────────────────┤
│ OTA metadata        │
├─────────────────────┤
│ ota_0               │
├─────────────────────┤
│ ota_1               │
└─────────────────────┘
```

If firmware is running from:

```text
ota_0
```

the update should be written to:

```text
ota_1
```

and vice versa.

### Success criteria

ESP32 can determine:

- Current running partition
- Next OTA update partition

---

# 11. Phase 7 — OTA_BEGIN

Linux opens the firmware image.

Calculate:

```text
firmware_size
SHA-256
```

Optionally extract firmware version from the ESP-IDF image.

Send:

```text
OTA_BEGIN
```

Payload:

```text
firmware_size
firmware_sha256
firmware_version
```

ESP32:

1. Check that no update is already active
2. Find the next OTA partition
3. Check firmware size
4. Call `esp_ota_begin()`
5. Initialize the OTA state
6. Send ACK

### Success criteria

ESP32 is ready to receive the firmware but has not yet changed the boot partition.

---

# 12. Phase 8 — OTA_DATA

Split firmware into chunks.

Recommended first chunk size:

```text
1024 bytes
```

Each packet contains:

```text
chunk_number
firmware_offset
chunk_data
```

Example flow:

```text
PC                                  ESP32

OTA_DATA chunk=0 ------------------>

                         write flash

                    <-------------- ACK chunk=0

OTA_DATA chunk=1 ------------------>

                         write flash

                    <-------------- ACK chunk=1
```

ESP32 calls:

```text
esp_ota_write()
```

for each valid chunk.

### Reliability

Every chunk should have:

- Packet sequence number
- Offset
- Packet CRC32
- ACK/NACK response

Possible NACK reasons:

```text
CRC_ERROR
INVALID_OFFSET
FLASH_WRITE_ERROR
OTA_NOT_STARTED
INVALID_LENGTH
TIMEOUT
```

### Success criteria

A complete firmware file can be transferred and written to the inactive OTA partition.

---

# 13. Phase 9 — OTA_END

After sending all firmware chunks:

```text
OTA_END
```

ESP32 performs:

1. Confirm expected byte count
2. Call `esp_ota_end()`
3. Validate image
4. Calculate/verify SHA-256
5. Compare hash against OTA_BEGIN metadata
6. Set new partition as boot partition
7. Send success response

Only after successful validation should the boot partition change.

### Success criteria

The inactive OTA partition contains a valid verified firmware image.

---

# 14. Phase 10 — Reboot Into New Firmware

Linux requests reboot.

ESP32 boots the newly selected OTA image.

After the serial device reconnects:

```text
Linux -> PING
Linux -> GET_INFO
```

Linux compares:

```text
expected firmware version
```

against:

```text
reported firmware version
```

Example:

```text
Before: 1.0.0
After:  1.1.0
```

### Success criteria

```bash
espctl update firmware.bin
```

ends by confirming that the expected firmware version is running.

---

# 15. Phase 11 — Host CLI

Desired Linux utility:

```bash
espctl --port /dev/ttyACM0 ping
```

```bash
espctl --port /dev/ttyACM0 info
```

```bash
espctl --port /dev/ttyACM0 reboot
```

```bash
espctl --port /dev/ttyACM0 update firmware.bin
```

Optional automatic port discovery:

```bash
espctl devices
```

Example update UI:

```text
Connecting...
Device: ESP32
Current firmware: 1.0.0

Firmware file:
Version: 1.1.0
Size: 842752 bytes
SHA256: ...

Starting update...

[####################] 100%

Firmware transferred
SHA256 verified
Image validated
Boot partition updated

Rebooting...

Waiting for device...

Connected
Running firmware: 1.1.0

Update successful
```

---

# 16. Phase 12 — Timeouts and Retries

Add host-side timeout handling.

Example:

```text
ACK timeout: 1 second
Retry limit: 3
```

For failed OTA chunks:

```text
send chunk
    |
    v
wait ACK
    |
    +--- ACK ---> next chunk
    |
    +--- NACK --> retry
    |
    +--- timeout -> retry
```

Do not immediately retry an entire firmware update because one packet failed.

### Success criteria

Updates survive occasional corrupted or dropped packets.

---

# 17. Phase 13 — OTA Abort

Implement:

```text
OTA_ABORT = 0x13
```

Used when:

- Host cancels update
- Transfer fails permanently
- Timeout occurs
- Firmware metadata is invalid
- Flash write fails

ESP32 should:

1. Abort OTA state
2. Release OTA resources
3. Keep current boot partition
4. Return to normal command mode

---

# 18. Phase 14 — Firmware Rollback

Enable ESP-IDF rollback support.

Desired behavior:

```text
Firmware 1.0
    |
    | OTA
    v
Firmware 1.1
    |
    | first boot
    v
self-test
   /   \
 OK     FAIL
 |        |
 v        v
confirm  rollback
```

New firmware should mark itself valid only after successful startup checks.

Possible checks:

- Required peripherals initialized
- Configuration loaded
- Main application task started
- No fatal startup errors

### Success criteria

A broken new firmware image can automatically fall back to the previous firmware.

---

# 19. Phase 15 — Security

CRC32 protects against accidental corruption.

CRC32 does **not** provide security.

Later add cryptographic verification.

Recommended direction:

```text
Signed firmware
```

The ESP32 should verify that firmware was signed by a trusted developer key.

Possible future security features:

- ESP-IDF secure boot
- Firmware signing
- Flash encryption
- Authenticated host commands
- Challenge-response authentication
- Anti-rollback firmware version

Do not create a custom cryptographic algorithm.

---

# 20. Firmware Versioning

Use semantic versions initially:

```text
MAJOR.MINOR.PATCH
```

Example:

```text
1.2.3
```

Also consider including:

```text
git commit
build timestamp
build type
```

Example:

```text
Version:     1.2.3
Git:         a193cb4
Build:       release
```

The ESP32 should obtain application metadata from the ESP-IDF application descriptor where possible.

---

# 21. Protocol Error Codes

Define explicit numeric error codes.

Example:

```text
0x00 OK

0x01 UNKNOWN_COMMAND
0x02 INVALID_PACKET
0x03 CRC_ERROR
0x04 INVALID_LENGTH
0x05 BUSY

0x10 OTA_ALREADY_STARTED
0x11 OTA_NOT_STARTED
0x12 OTA_INVALID_OFFSET
0x13 OTA_FLASH_ERROR
0x14 OTA_HASH_MISMATCH
0x15 OTA_INVALID_IMAGE
0x16 OTA_SIZE_ERROR
```

Return numeric codes over the protocol.

The Linux tool translates those codes into readable messages.

---

# 22. Testing Strategy

## Protocol tests

Test:

- Empty packet
- Minimum packet
- Maximum packet
- Invalid magic
- Wrong CRC
- Wrong length
- Truncated packet
- Back-to-back packets
- Random garbage between packets
- Fragmented packet delivery

## OTA tests

Test:

- Normal update
- Same version update
- Larger firmware
- Corrupted firmware
- Incorrect SHA-256
- Disconnect during update
- Reconnect after failure
- Invalid offset
- Duplicate chunk
- Missing chunk
- ESP32 reset during update
- Host crash during update

## Recovery test

Always verify that an interrupted update leaves the currently running firmware bootable.

---

# 23. Milestones

## Milestone 1 — Raw Serial

```text
PC <-> ESP32 raw bytes
```

## Milestone 2 — Protocol

```text
Packet framing
CRC32
Parser
ACK/NACK
```

## Milestone 3 — Device Management

```text
PING
GET_INFO
REBOOT
```

## Milestone 4 — OTA Transport

```text
OTA_BEGIN
OTA_DATA
OTA_END
OTA_ABORT
```

## Milestone 5 — Complete Update

```text
firmware.bin
    ↓
Linux espctl
    ↓
serial protocol
    ↓
ESP32 OTA partition
    ↓
verification
    ↓
reboot
    ↓
new firmware
```

## Milestone 6 — Reliability

```text
timeouts
retries
rollback
error handling
```

## Milestone 7 — Security

```text
firmware signing
secure boot
authentication
```

---

# 24. First Implementation Target

Do **not** start by implementing OTA immediately.

Start with:

```text
PING
GET_INFO
```

First target:

```bash
$ python3 espctl.py info
```

Expected result:

```text
Connected to ESP32

Firmware version: 0.1.0
Protocol version: 1
Chip: ESP32
Running partition: factory/ota_0
Flash size: ...
Uptime: ...
```

Once `GET_INFO` works reliably using binary packets, add:

```text
REBOOT
```

Then begin the OTA commands.

---

# 25. Definition of Done

The project is complete when this works reliably:

```bash
$ espctl info
Firmware: 1.0.0

$ espctl update firmware-1.1.0.bin

Connecting...
Starting OTA...
Uploading...
Verifying...
Rebooting...
Reconnecting...

Update successful.

$ espctl info
Firmware: 1.1.0
```

And all communication between the PC and ESP32 uses the custom binary protocol rather than text commands.
