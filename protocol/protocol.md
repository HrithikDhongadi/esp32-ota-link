# Serial Binary Protocol

All multi-byte integer fields are little-endian.

## Packet

```text
0      2      3      4      6       8          8+N
+------+------+------+------+-------+----------+------+
|MAGIC | VER  | CMD  | SEQ  | LENGTH| PAYLOAD  | CRC  |
+------+------+------+------+-------+----------+------+
|2 byte|1 byte|1 byte|2 byte|2 byte | N bytes  |4 byte|
```

- `MAGIC`: `0xAA55`, sent as bytes `AA 55`
- `VER`: protocol version, currently `1`
- `CMD`: command or response identifier
- `SEQ`: host-selected sequence number; response echoes it
- `LENGTH`: payload length in bytes
- `CRC`: CRC32 over `VER`, `CMD`, `SEQ`, `LENGTH`, and `PAYLOAD`

Maximum payload for version 1 is 1024 bytes.

## Commands

```text
0x01 PING
0x02 GET_INFO
0x03 REBOOT

0x10 OTA_BEGIN
0x11 OTA_DATA
0x12 OTA_END
0x13 OTA_ABORT

0xF0 ACK
0xF1 NACK
0xF2 INFO
```

## ACK Payload

```text
status: u8
```

`status == 0` means success.

## NACK Payload

```text
error_code: u8
```

## GET_INFO Response Payload

Version 1 uses compact length-prefixed strings:

```text
protocol_version: u8
firmware_major: u8
firmware_minor: u8
firmware_patch: u8
uptime_seconds: u32
free_heap: u32
boot_count: u32
chip_model: str8
running_partition: str8
idf_version: str8
project_name: str8
build_date: str8
```

`str8` means:

```text
length: u8
bytes: UTF-8, not null terminated
```

## OTA_BEGIN Payload

```text
firmware_size: u32
firmware_image_sha256: 32 bytes
```

`OTA_BEGIN` prepares the inactive OTA partition with `esp_ota_begin()`. It does
not write firmware bytes and does not change the boot partition.

## OTA_DATA Payload

```text
chunk_number: u32
firmware_offset: u32
chunk_data: 1..1016 bytes
```

Each `OTA_DATA` packet writes one firmware chunk to the active OTA handle. The
ESP32 rejects chunks whose offset does not match the next expected byte.

## OTA_END Payload

No payload.

`OTA_END` validates the staged image, compares the ESP-IDF app image SHA-256,
sets the new boot partition, and clears OTA state. It does not reboot by itself.

## OTA_ABORT Payload

No payload.

`OTA_ABORT` cancels an active OTA session and keeps the current boot partition.
