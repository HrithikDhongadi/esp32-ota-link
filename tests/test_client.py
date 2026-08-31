import hashlib
import hmac
from pathlib import Path

import pytest

from host.otalink import (
    AUTH_PROOF_PREFIX,
    AUTH_DEVICE_PROOF_PREFIX,
    AUTH_RESPONSE_PREFIX,
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
            proof = hmac.new(
                self.key,
                AUTH_DEVICE_PROOF_PREFIX + self.client_nonce + self.server_nonce,
                hashlib.sha256,
            ).digest()[:AUTH_TAG_SIZE]
            return Packet(Command.ACK, sequence, bytes([ErrorCode.OK]) + self.server_nonce + proof)

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
        return self._signed_response(request.command, Command.ACK, sequence, bytes([ErrorCode.OK]))

    def close(self) -> None:
        pass

    def _signed_response(
        self,
        request_command: Command,
        response_command: Command,
        sequence: int,
        payload: bytes,
    ) -> Packet:
        tag = hmac.new(
            self.session_key,
            AUTH_RESPONSE_PREFIX +
            bytes([request_command]) +
            bytes([response_command]) +
            sequence.to_bytes(2, "little") +
            payload,
            hashlib.sha256,
        ).digest()[:AUTH_TAG_SIZE]
        return Packet(response_command, sequence, payload + tag)


class ResetAuthTransport(AuthTransport):
    def __init__(self, key: bytes) -> None:
        super().__init__(key)
        self.rejected_once = False
        self.auth_challenges = 0

    def request(self, packet: bytes, sequence: int, timeout: float) -> Packet:
        parsed = self.parser.feed(packet)
        assert len(parsed) == 1
        request = parsed[0]

        if request.command == Command.AUTH and len(request.payload) == AUTH_NONCE_SIZE:
            self.auth_challenges += 1
            self.client_nonce = request.payload
            self.session_key = hmac.new(
                self.key,
                AUTH_SESSION_PREFIX + self.client_nonce + self.server_nonce,
                hashlib.sha256,
            ).digest()
            proof = hmac.new(
                self.key,
                AUTH_DEVICE_PROOF_PREFIX + self.client_nonce + self.server_nonce,
                hashlib.sha256,
            ).digest()[:AUTH_TAG_SIZE]
            return Packet(Command.ACK, sequence, bytes([ErrorCode.OK]) + self.server_nonce + proof)

        if request.command == Command.AUTH and len(request.payload) == AUTH_NONCE_SIZE + AUTH_TAG_SIZE:
            return Packet(Command.ACK, sequence, bytes([ErrorCode.OK]))

        if not self.rejected_once:
            self.rejected_once = True
            return Packet(Command.NACK, sequence, bytes([ErrorCode.AUTH_REQUIRED]))

        self.protected_request = request
        return self._signed_response(request.command, Command.ACK, sequence, bytes([ErrorCode.OK]))


class FakeDeviceTransport(AuthTransport):
    def __init__(self, key: bytes) -> None:
        super().__init__(key)

    def request(self, packet: bytes, sequence: int, timeout: float) -> Packet:
        parsed = self.parser.feed(packet)
        assert len(parsed) == 1
        request = parsed[0]
        if request.command == Command.AUTH and len(request.payload) == AUTH_NONCE_SIZE:
            return Packet(
                Command.ACK,
                sequence,
                bytes([ErrorCode.OK]) + self.server_nonce + (b"\x00" * AUTH_TAG_SIZE),
            )
        return Packet(Command.ACK, sequence, bytes([ErrorCode.OK]))


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
    assert first.sequence == second.sequence
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


def test_authenticated_client_reauthenticates_after_device_reset():
    key = b"test secret"
    transport = ResetAuthTransport(key)
    client = OtaLinkClient(
        port="unused",
        baud=115200,
        timeout=1.0,
        retries=2,
        transport=transport,
        auth_key=key,
    )

    packet = client.request(Command.REBOOT)

    assert packet.command == Command.ACK
    assert transport.auth_challenges == 2


def test_authenticated_client_rejects_fake_device_proof():
    key = b"test secret"
    transport = FakeDeviceTransport(key)
    client = OtaLinkClient(
        port="unused",
        baud=115200,
        timeout=1.0,
        retries=1,
        transport=transport,
        auth_key=key,
    )

    with pytest.raises(SystemExit, match="invalid device proof"):
        client.request(Command.REBOOT)


def test_authenticated_client_rejects_unsigned_protected_response():
    key = b"test secret"
    transport = AuthTransport(key)
    original_signed_response = transport._signed_response
    transport._signed_response = lambda *_args: Packet(Command.ACK, _args[2], bytes([ErrorCode.OK]))  # type: ignore[method-assign]
    client = OtaLinkClient(
        port="unused",
        baud=115200,
        timeout=1.0,
        retries=1,
        transport=transport,
        auth_key=key,
    )

    with pytest.raises(SystemExit, match="missing its tag"):
        client.request(Command.REBOOT)

    transport._signed_response = original_signed_response  # type: ignore[method-assign]


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
