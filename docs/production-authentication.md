# Production authentication guide

`esp32-ota-link` includes optional link authentication for commands that can
change device state. This guide explains what it does, how to configure it for
real deployments, and what security controls you should still add around it.

## What this authentication protects

When `require_authentication` is enabled, these commands require a valid
session:

- `REBOOT`
- `ROLLBACK`
- `OTA_BEGIN`
- `OTA_DATA`
- `OTA_END`
- `OTA_ABORT`

The discovery commands remain public:

- `PING`
- `GET_INFO`
- `AUTH`

That split lets a host discover a device and then authenticate before doing
anything dangerous.

## What it does not replace

Link authentication protects the OTA control link. It does not prove that a
firmware image itself is trusted.

For production devices, use link authentication together with:

- ESP-IDF secure boot
- signed firmware images
- flash encryption, especially if the OTA auth key is compiled into firmware
- physical/debug-port protections appropriate for your product

Good mental model:

```text
link authentication = who may ask the device to update
firmware signing    = what image the device may boot
```

You usually want both.

## Do not use `service-secret` in production

`service-secret` is only the example key used by the demo app and tests. It is
not a safe production secret.

Generate a random key instead:

```bash
openssl rand -hex 32
```

That gives you a 32-byte key encoded as 64 hex characters, for example:

```text
3f4f6c58c8ad7f2e9d4be7f937f0a3d3e4cf5ad3d5d65a1a1b61e9fd8a77a5f2
```

Prefer:

- unique keys per device, customer, site, or production batch
- storage in a real secrets manager on the host/service side
- no real keys committed to Git

## Firmware configuration

In a product app, pass a pre-shared key to `ota_link_start_with_config()`:

```c
#include "ota_link.h"

static const uint8_t ota_auth_key[] = {
    0x3f, 0x4f, 0x6c, 0x58, 0xc8, 0xad, 0x7f, 0x2e,
    0x9d, 0x4b, 0xe7, 0xf9, 0x37, 0xf0, 0xa3, 0xd3,
    0xe4, 0xcf, 0x5a, 0xd3, 0xd5, 0xd6, 0x5a, 0x1a,
    0x1b, 0x61, 0xe9, 0xfd, 0x8a, 0x77, 0xa5, 0xf2,
};

void app_main(void)
{
    ota_link_config_t config = OTA_LINK_DEFAULT_CONFIG();

    config.auth_key = ota_auth_key;
    config.auth_key_len = sizeof(ota_auth_key);
    config.require_authentication = true;

    ESP_ERROR_CHECK(ota_link_start_with_config(&config));
}
```

If you use the example app, you can enable auth through menuconfig:

```bash
cd firmware
idf.py -B build_esp32c3_4mb_2ota menuconfig
```

Then enable:

```text
esp32_ota_link example
  [*] Require OTA link authentication in the example app
```

The example key defaults to `service-secret`; change it before using the
example firmware outside local testing.

Rebuild and flash after changing config:

```bash
idf.py -B build_esp32c3_4mb_2ota build
idf.py -B build_esp32c3_4mb_2ota -p /dev/ttyUSB0 flash
```

## Host usage

For text keys:

```bash
python3 -m host.otalink \
  --port /dev/ttyUSB0 \
  --auth-key 'service-secret' \
  info
```

For binary/random keys, use `hex:`:

```bash
python3 -m host.otalink \
  --port /dev/ttyUSB0 \
  --auth-key hex:3f4f6c58c8ad7f2e9d4be7f937f0a3d3e4cf5ad3d5d65a1a1b61e9fd8a77a5f2 \
  update firmware/build_esp32c3_4mb_2ota/esp32_ota_link.bin
```

Shell mode also accepts the key at startup:

```bash
python3 -m host.otalink \
  --port /dev/ttyUSB0 \
  --auth-key hex:3f4f6c58c8ad7f2e9d4be7f937f0a3d3e4cf5ad3d5d65a1a1b61e9fd8a77a5f2 \
  shell
```

Then protected commands inside the shell use the authenticated session:

```text
otalink> update firmware/build_esp32c3_4mb_2ota/esp32_ota_link.bin
otalink> reboot
otalink> info
```

## How the handshake works

The key is never sent over the wire.

```text
Host   -> Device: AUTH(client_nonce)
Device -> Host:   ACK(OK, server_nonce, device_proof)
Host   -> Device: AUTH(client_nonce, host_proof)
Device -> Host:   ACK(OK)
```

The proofs are truncated HMAC-SHA256 tags:

```text
device_proof = HMAC-SHA256(key, "device-proof" || client_nonce || server_nonce)[0:16]
host_proof   = HMAC-SHA256(key, "auth-proof"   || client_nonce || server_nonce)[0:16]
session_key  = HMAC-SHA256(key, "session"      || client_nonce || server_nonce)
```

The device proof prevents a fake device from acknowledging commands unless it
knows the key. The host proof prevents unauthenticated hosts from running
protected commands.

## Command and response authentication

After the handshake, protected command payloads carry a 16-byte tag:

```text
request_payload = original_payload || request_tag
request_tag     = HMAC-SHA256(session_key, command || sequence || original_payload)[0:16]
```

Protected responses also carry a 16-byte tag:

```text
response_payload = original_response_payload || response_tag
response_tag     = HMAC-SHA256(
                     session_key,
                     "response" || request_command || response_command ||
                     sequence || original_response_payload
                   )[0:16]
```

The host verifies the response tag before accepting an ACK or NACK.

## Retry and replay behavior

The host retries timed-out commands by sending the exact same packet with the
same sequence number. The device keeps a small cache of recent authenticated
requests and their responses.

If a response was lost:

```text
Host sends OTA_END
Device finalizes OTA and sends ACK
ACK is lost
Host retries the same OTA_END packet
Device resends cached ACK without running OTA_END again
```

If someone reuses a sequence number with different packet bytes, the device
rejects it as an authentication failure.

## Operational recommendations

- Use a 32-byte random key or stronger.
- Rotate keys per product line, site, customer, or device when practical.
- Keep real keys out of source control.
- Store host-side keys in a secrets manager or protected service config.
- Enable flash encryption if the key is compiled into firmware.
- Enable secure boot and signed firmware images for image authenticity.
- Prefer a dedicated UART or trusted transport for production update traffic.
- Treat UART0 carefully if it is shared with ESP-IDF logs.

