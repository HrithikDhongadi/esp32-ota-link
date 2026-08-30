# Firmware Profiles

Ready-made ESP-IDF profile files for common ESP32 development boards and
module layouts.

Each profile has:

- a partition CSV in `profiles/partitions/`
- a matching sdkconfig defaults file in `profiles/sdkconfig/`

The project root `partitions.csv` and `sdkconfig.defaults` remain the tested
default for ESP32-WROOM-32 with 4 MB flash.

## Profiles

| Board / Module Family | Profile | Target | Flash | PSRAM | OTA Slots | App Slot Size |
| --- | --- | --- | --- | --- | --- | --- |
| ESP32 DevKitC / DevKitM / WROOM | `esp32_4mb_2ota` | `esp32` | 4 MB | none | 2 | 1408 KB |
| ESP32 DevKitC / DevKitM / WROOM | `esp32_8mb_2ota` | `esp32` | 8 MB | board-specific | 2 | 3 MB |
| ESP32 DevKitC / DevKitM / WROVER | `esp32_wrover_4mb_2ota` | `esp32` | 4 MB | 4 MB or 8 MB QSPI | 2 | 1408 KB |
| ESP32-S2-DevKit / Saola / WROOM | `esp32s2_4mb_2ota` | `esp32s2` | 4 MB | none | 2 | 1408 KB |
| ESP32-S3-DevKitC / DevKitM / WROOM-1-N16R8 | `esp32s3_16mb_2ota` | `esp32s3` | 16 MB | 8 MB OPI | 2 | 7 MB |
| ESP32-S3-DevKitC / DevKitM / WROOM-1-N16R8 | `esp32s3_16mb_3ota` | `esp32s3` | 16 MB | 8 MB OPI | 3 | 4 MB |
| ESP32-C3-DevKitM-1 | `esp32c3_4mb_2ota` | `esp32c3` | 4 MB | none | 2 | 1408 KB |
| ESP32-C5 DevKit | `esp32c5_4mb_2ota` | `esp32c5` | 4 MB | none | 2 | 1408 KB |
| ESP32-C6-DevKitC | `esp32c6_4mb_2ota` | `esp32c6` | 4 MB | none | 2 | 1408 KB |
| ESP32-H2 DevKit | `esp32h2_4mb_2ota` | `esp32h2` | 4 MB | none | 2 | 1408 KB |
| ESP32-P4 DevKit / module | `esp32p4_16mb_2ota` | `esp32p4` | 16 MB | board-specific | 2 | 7 MB |

## Build With A Profile

Use a separate build directory and sdkconfig file when switching chips. This
keeps the repo's default `sdkconfig` untouched.

ESP32-WROOM-32, 4 MB flash:

```bash
idf.py \
  -B build_esp32_4mb \
  -D SDKCONFIG=build_esp32_4mb/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32_4mb_2ota.defaults" \
  set-target esp32 build
```

ESP32 with 8 MB flash:

```bash
idf.py \
  -B build_esp32_8mb \
  -D SDKCONFIG=build_esp32_8mb/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32_8mb_2ota.defaults" \
  set-target esp32 build
```

ESP32-WROVER, 4 MB flash, QSPI PSRAM:

```bash
idf.py \
  -B build_esp32_wrover_4mb \
  -D SDKCONFIG=build_esp32_wrover_4mb/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32_wrover_4mb_2ota.defaults" \
  set-target esp32 build
```

ESP32-S2, 4 MB flash:

```bash
idf.py \
  -B build_esp32s2_4mb \
  -D SDKCONFIG=build_esp32s2_4mb/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32s2_4mb_2ota.defaults" \
  set-target esp32s2 build
```

ESP32-S3-WROOM-1-N16R8, 16 MB flash, 8 MB PSRAM, two OTA slots:

```bash
idf.py \
  -B build_esp32s3_16mb_2ota \
  -D SDKCONFIG=build_esp32s3_16mb_2ota/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32s3_16mb_2ota.defaults" \
  set-target esp32s3 build
```

ESP32-S3-WROOM-1-N16R8, 16 MB flash, 8 MB PSRAM, three OTA slots:

```bash
idf.py \
  -B build_esp32s3_16mb_3ota \
  -D SDKCONFIG=build_esp32s3_16mb_3ota/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32s3_16mb_3ota.defaults" \
  set-target esp32s3 build
```

ESP32-C3-DevKitM-1, 4 MB flash:

```bash
idf.py \
  -B build_esp32c3_4mb \
  -D SDKCONFIG=build_esp32c3_4mb/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32c3_4mb_2ota.defaults" \
  set-target esp32c3 build
```

ESP32-C5, 4 MB flash:

```bash
idf.py \
  -B build_esp32c5_4mb \
  -D SDKCONFIG=build_esp32c5_4mb/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32c5_4mb_2ota.defaults" \
  set-target esp32c5 build
```

ESP32-C6-DevKitC, 4 MB flash:

```bash
idf.py \
  -B build_esp32c6_4mb \
  -D SDKCONFIG=build_esp32c6_4mb/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32c6_4mb_2ota.defaults" \
  set-target esp32c6 build
```

ESP32-H2, 4 MB flash:

```bash
idf.py \
  -B build_esp32h2_4mb \
  -D SDKCONFIG=build_esp32h2_4mb/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32h2_4mb_2ota.defaults" \
  set-target esp32h2 build
```

ESP32-P4, 16 MB flash:

```bash
idf.py \
  -B build_esp32p4_16mb \
  -D SDKCONFIG=build_esp32p4_16mb/sdkconfig \
  -D SDKCONFIG_DEFAULTS="profiles/sdkconfig/common.defaults;profiles/sdkconfig/esp32p4_16mb_2ota.defaults" \
  set-target esp32p4 build
```

## Notes

- Confirm the real flash size from your boot log before choosing a profile.
- ESP32 DevKitC and DevKitM boards usually use WROOM modules without PSRAM.
- ESP32 WROVER boards include external PSRAM; use the WROVER profile as a
  starting point.
- ESP32-S2 is single-core and has no Bluetooth, but this OTA link component
  does not depend on Bluetooth.
- ESP32-S3-WROOM-1-N16R8 uses 16 MB flash and 8 MB PSRAM.
- ESP32-S3-WROOM-1 N16R8 modules use QSPI flash and OPI PSRAM.
- ESP32-H2 is not a WiFi chip, but UART/USB-style OTA still works for local
  service updates.
- ESP32-P4 boards often include larger flash and PSRAM; this starter profile
  only fixes the flash layout and leaves PSRAM tuning to the exact board.
- Some ESP32-family boards ship with different flash sizes. Copy the closest
  profile and resize the OTA slots if your board is not the listed size.
- You can copy and modify any profile for your own board.
- `otalink info` discovers OTA app partitions dynamically, so additional OTA
  slots are reported automatically.
