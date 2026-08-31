import hashlib
import hmac
from pathlib import Path

import pytest

from host.otalink import (
    AUTH_PROOF_PREFIX,
    AUTH_SESSION_PREFIX,
    OtaLinkClient,
    firmware_metadata,
    parse_auth_key,
)
from host.protocol import (
    AUTH_OTA_CHUNK_DATA_SIZE,
    AUTH_NONCE_SIZE,
    AUTH_TAG_SIZE,
    Command,
    ErrorCode,
    OTA_CHUNK_DATA_SIZE,
    Packet,
    PacketParser,
    build_ota_data_payload,
)


class FlakyTransport:
    def __init__(self) -> None:
        self.parser = PacketParser()
        self.requests = []

    def request(self, packet: bytes, sequence: int, timeout: float) -> Packet:
        parsed = self.parser.feed(packet)
        assert len(parsed) == 1
        self.requests.append((parsed[0], timeout))
        if len(self.requests) == 1:
            raise TimeoutError("simulated lost ACK")
        return Packet(Command.ACK, sequence, bytes([ErrorCode.OK]))

    def close(self) -> None:
        pass


class AuthTransport:
    def __init__(self, key: bytes) -> None:
        self.key = key
        self.parser = PacketParser()
        self.client_nonce = b""
        self.server_nonce = bytes(range(AUTH_NONCE_SIZE))
        self.session_key = b""
        self.protected_request: Packet | None = None

    def request(self, packet: bytes, sequence: int, timeout: float) -> Packet:
        parsed = self.parser.feed(packet)
        assert len(parsed) == 1
        request = parsed[0]

        if request.command == Command.AUTH and len(request.payload) == AUTH_NONCE_SIZE:
            self.client_nonce = request.payload
            self.session_key = hmac.new(
                self.key,
                AUTH_SESSION_PREFIX + self.client_nonce + self.server_nonce,
                hashlib.sha256,
            ).digest()
            return Packet(Command.ACK, sequence, bytes([ErrorCode.OK]) + self.server_nonce)

        if request.command == Command.AUTH and len(request.payload) == AUTH_NONCE_SIZE + AUTH_TAG_SIZE:
            proof = hmac.new(
                self.key,
                AUTH_PROOF_PREFIX + self.client_nonce + self.server_nonce,
                hashlib.sha256,
            ).digest()[:AUTH_TAG_SIZE]
            assert request.payload == self.client_nonce + proof
            return Packet(Command.ACK, sequence, bytes([ErrorCode.OK]))

        assert request.command == Command.REBOOT
        self.protected_request = request
        payload = request.payload[:-AUTH_TAG_SIZE]
        tag = request.payload[-AUTH_TAG_SIZE:]
        expected = hmac.new(
            self.session_key,
            bytes([request.command]) + request.sequence.to_bytes(2, "little") + payload,
            hashlib.sha256,
        ).digest()[:AUTH_TAG_SIZE]
        assert tag == expected
        return Packet(Command.ACK, sequence, bytes([ErrorCode.OK]))

    def close(self) -> None:
        pass


def test_client_retries_same_payload_after_timeout():
    transport = FlakyTransport()
    client = OtaLinkClient(
        port="unused",
        baud=115200,
        timeout=1.0,
        retries=2,
        transport=transport,
    )
    payload = build_ota_data_payload(3, 2032, b"firmware")

    packet = client.request(Command.OTA_DATA, payload)

    assert packet.command == Command.ACK
    first = transport.requests[0][0]
    second = transport.requests[1][0]
    assert first.command == Command.OTA_DATA
    assert second.command == Command.OTA_DATA
    assert first.sequence != second.sequence
    assert first.payload == second.payload == payload


def test_authenticated_client_challenges_and_tags_protected_command():
    key = b"test secret"
    transport = AuthTransport(key)
    client = OtaLinkClient(
        port="unused",
        baud=115200,
        timeout=1.0,
        retries=1,
        transport=transport,
        auth_key=key,
    )

    packet = client.request(Command.REBOOT)

    assert packet.command == Command.ACK
    assert transport.protected_request is not None
    assert len(transport.protected_request.payload) == AUTH_TAG_SIZE


def test_authenticated_client_uses_smaller_ota_chunks():
    client = OtaLinkClient(
        port="unused",
        baud=115200,
        timeout=1.0,
        retries=1,
        transport=FlakyTransport(),
        auth_key=b"secret",
    )

    assert client.max_ota_chunk_data_size() == AUTH_OTA_CHUNK_DATA_SIZE
    assert client.max_ota_chunk_data_size() == OTA_CHUNK_DATA_SIZE - AUTH_TAG_SIZE


def test_parse_auth_key_supports_text_and_hex():
    assert parse_auth_key("secret") == b"secret"
    assert parse_auth_key("hex:736563726574") == b"secret"


def test_firmware_metadata_reports_file_and_embedded_image_hash(tmp_path: Path):
    image_hash = bytes(range(32))
    firmware = tmp_path / "app.bin"
    firmware.write_bytes(b"esp32-image" + image_hash)

    size, file_hash, embedded_hash = firmware_metadata(firmware)

    assert size == len(b"esp32-image") + len(image_hash)
    assert file_hash == hashlib.sha256(firmware.read_bytes()).digest()
    assert embedded_hash == image_hash


def test_firmware_metadata_rejects_tiny_file(tmp_path: Path):
    firmware = tmp_path / "tiny.bin"
    firmware.write_bytes(b"too small")

    with pytest.raises(SystemExit):
        firmware_metadata(firmware)
