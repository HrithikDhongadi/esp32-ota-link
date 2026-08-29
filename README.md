# esp32-serial-ota

This project builds a binary serial protocol between a Linux host and an ESP32.
The first milestone is reliable packet transport plus basic device-management
commands. OTA support is intentionally staged after `PING`, `GET_INFO`, and
`REBOOT` work reliably.

## Layout

```text
firmware/   ESP-IDF application for the ESP32
host/       Python host CLI and packet implementation
protocol/   Protocol notes and wire format
tests/      Host-side protocol tests
plan.md     Original project roadmap
```

## Current Milestone

Implemented foundation:

- Binary packet encoder
- Streaming packet parser
- CRC32 validation
- Host CLI command skeleton
- ESP-IDF command handler skeleton for `PING`, `GET_INFO`, and `REBOOT`

Next hardware step:

```bash
cd esp32-serial-ota/firmware
idf.py set-target esp32
idf.py build flash monitor
```

Then, from another terminal:

```bash
cd esp32-serial-ota
python3 -m host.espctl --port /dev/ttyUSB0 ping
python3 -m host.espctl --port /dev/ttyUSB0 info
```
