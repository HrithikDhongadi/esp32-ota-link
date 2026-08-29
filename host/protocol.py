from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct
import zlib


MAGIC = b"\xAA\x55"
VERSION = 1
MAX_PAYLOAD = 1024
OTA_DATA_HEADER_SIZE = 8
OTA_CHUNK_DATA_SIZE = MAX_PAYLOAD - OTA_DATA_HEADER_SIZE
HEADER = struct.Struct("<2sBBHH")
CRC = struct.Struct("<I")
OTA_BEGIN = struct.Struct("<I32s")
OTA_DATA = struct.Struct("<II")


class Command(IntEnum):
    PING = 0x01
    GET_INFO = 0x02
    REBOOT = 0x03
    OTA_BEGIN = 0x10
    OTA_DATA = 0x11
    OTA_END = 0x12
    OTA_ABORT = 0x13
    ACK = 0xF0
    NACK = 0xF1
    INFO = 0xF2


class ErrorCode(IntEnum):
    OK = 0x00
    UNKNOWN_COMMAND = 0x01
    INVALID_PACKET = 0x02
    CRC_ERROR = 0x03
    INVALID_LENGTH = 0x04
    BUSY = 0x05
    OTA_ALREADY_STARTED = 0x10
    OTA_NOT_STARTED = 0x11
    OTA_INVALID_OFFSET = 0x12
    OTA_FLASH_ERROR = 0x13
    OTA_HASH_MISMATCH = 0x14
    OTA_INVALID_IMAGE = 0x15
    OTA_SIZE_ERROR = 0x16


@dataclass(frozen=True)
class Packet:
    command: int
    sequence: int
    payload: bytes = b""
    version: int = VERSION


class ProtocolError(Exception):
    pass


class PacketTooLarge(ProtocolError):
    pass


def _crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def build_packet(command: int, sequence: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise PacketTooLarge(f"payload exceeds {MAX_PAYLOAD} bytes")
    if not 0 <= sequence <= 0xFFFF:
        raise ValueError("sequence must fit in uint16")

    header = HEADER.pack(MAGIC, VERSION, command, sequence, len(payload))
    crc_input = header[2:] + payload
    return header + payload + CRC.pack(_crc32(crc_input))


def build_ota_begin_payload(firmware_size: int, sha256: bytes) -> bytes:
    if not 0 < firmware_size <= 0xFFFFFFFF:
        raise ValueError("firmware_size must fit in uint32")
    if len(sha256) != 32:
        raise ValueError("sha256 must be exactly 32 bytes")
    return OTA_BEGIN.pack(firmware_size, sha256)


def build_ota_data_payload(chunk_number: int, offset: int, data: bytes) -> bytes:
    if not 0 <= chunk_number <= 0xFFFFFFFF:
        raise ValueError("chunk_number must fit in uint32")
    if not 0 <= offset <= 0xFFFFFFFF:
        raise ValueError("offset must fit in uint32")
    if len(data) > OTA_CHUNK_DATA_SIZE:
        raise PacketTooLarge(f"chunk data exceeds {OTA_CHUNK_DATA_SIZE} bytes")
    if len(data) == 0:
        raise ValueError("chunk data cannot be empty")
    return OTA_DATA.pack(chunk_number, offset) + data


class PacketParser:
    """Streaming parser that accepts fragmented input and ignores leading noise."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[Packet]:
        self._buffer.extend(data)
        packets: list[Packet] = []

        while True:
            magic_index = self._buffer.find(MAGIC)
            if magic_index < 0:
                self._keep_possible_partial_magic()
                return packets
            if magic_index:
                del self._buffer[:magic_index]

            if len(self._buffer) < HEADER.size:
                return packets

            magic, version, command, sequence, length = HEADER.unpack(
                self._buffer[: HEADER.size]
            )
            if magic != MAGIC:
                del self._buffer[0]
                continue
            if length > MAX_PAYLOAD:
                del self._buffer[:2]
                continue

            packet_size = HEADER.size + length + CRC.size
            if len(self._buffer) < packet_size:
                return packets

            raw = bytes(self._buffer[:packet_size])
            payload = raw[HEADER.size : HEADER.size + length]
            expected_crc = CRC.unpack(raw[-CRC.size :])[0]
            actual_crc = _crc32(raw[2:-CRC.size])
            del self._buffer[:packet_size]

            if actual_crc != expected_crc:
                continue

            packets.append(Packet(command, sequence, payload, version))

    def _keep_possible_partial_magic(self) -> None:
        if self._buffer.endswith(MAGIC[:1]):
            del self._buffer[:-1]
        else:
            self._buffer.clear()


def read_u8_string(data: bytes, offset: int) -> tuple[str, int]:
    if offset >= len(data):
        raise ProtocolError("missing string length")
    length = data[offset]
    offset += 1
    end = offset + length
    if end > len(data):
        raise ProtocolError("truncated string")
    return data[offset:end].decode("utf-8"), end


def parse_info_payload(payload: bytes) -> dict[str, object]:
    if len(payload) < 16:
        raise ProtocolError("info payload too short")

    protocol_version, major, minor, patch, uptime, free_heap, boot_count = struct.unpack_from(
        "<BBBBIII", payload, 0
    )
    offset = 16
    chip_model, offset = read_u8_string(payload, offset)
    running_partition, offset = read_u8_string(payload, offset)
    idf_version, offset = read_u8_string(payload, offset)
    project_name, offset = read_u8_string(payload, offset)
    build_date, offset = read_u8_string(payload, offset)

    return {
        "protocol_version": protocol_version,
        "firmware": f"{major}.{minor}.{patch}",
        "uptime_seconds": uptime,
        "free_heap": free_heap,
        "boot_count": boot_count,
        "chip_model": chip_model,
        "running_partition": running_partition,
        "idf_version": idf_version,
        "project_name": project_name,
        "build_date": build_date,
    }
