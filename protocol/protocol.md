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
0x04 ROLLBACK
0x05 AUTH

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

`0x06 AUTH_REQUIRED` means the command needs a valid authentication session or
the packet authentication tag was invalid.

## Optional Authentication

Devices may require authentication for mutating commands. `PING`, `GET_INFO`,
and `AUTH` remain available before authentication so hosts can discover and
establish a session.

Protected commands are `REBOOT`, `ROLLBACK`, `OTA_BEGIN`, `OTA_DATA`,
`OTA_END`, and `OTA_ABORT`.

Authentication uses a pre-shared key and HMAC-SHA256:

```text
Host -> device AUTH: client_nonce: 16 bytes
Device -> host ACK:  status: u8, server_nonce: 16 bytes
Host -> device AUTH: client_nonce: 16 bytes, proof_tag: 16 bytes
Device -> host ACK:  status: u8
```

The proof tag is the first 16 bytes of:

```text
HMAC-SHA256(auth_key, "auth-proof" || client_nonce || server_nonce)
```

Both sides derive a session key:

```text
HMAC-SHA256(auth_key, "session" || client_nonce || server_nonce)
```

Authenticated command payloads append a 16-byte tag to the original command
payload:

```text
payload: original_payload || auth_tag
auth_tag: first 16 bytes of HMAC-SHA256(session_key, command || sequence || original_payload)
```

`sequence` is encoded little-endian. Because the tag consumes 16 payload bytes,
authenticated `OTA_DATA` chunks carry up to 1000 firmware bytes.

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
rollback_possible: u8
chip_model: str8
running_partition: str8
boot_partition: str8
ota_state: str8
idf_version: str8
project_name: str8
build_date: str8
ota_slot_count: u8
ota_slot_label: str8
ota_slot_state: str8
...
```

The OTA slot list is dynamic. Devices report every app partition whose subtype
is in ESP-IDF's OTA range, so partition tables with `ota_0`, `ota_1`, `ota_2`,
and additional OTA slots can be represented without changing the host protocol.

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
chunk_data: 1..1016 bytes unauthenticated, 1..1000 bytes authenticated
```

Each `OTA_DATA` packet writes one firmware chunk to the active OTA handle. The
ESP32 accepts an exact duplicate of the most recently written chunk so the host
can retry safely if the ACK is lost. Other offsets are rejected.

## OTA_END Payload

No payload.

`OTA_END` validates the staged image, compares the ESP-IDF app image SHA-256,
sets the new boot partition, and clears OTA state. It does not reboot by itself.

## OTA_ABORT Payload

No payload.

`OTA_ABORT` cancels an active OTA session and keeps the current boot partition.

## ROLLBACK Payload

No payload.

`ROLLBACK` marks the running app invalid and reboots into the previous valid OTA
partition. The device rejects this command if an OTA session is active or if
ESP-IDF reports that rollback is not available.
