import struct

from host.protocol import (
    Command,
    PacketParser,
    OTA_CHUNK_DATA_SIZE,
    build_ota_data_payload,
    build_ota_begin_payload,
    build_packet,
    parse_info_payload,
)


def test_build_and_parse_minimum_packet():
    raw = build_packet(Command.PING, 7)
    packets = PacketParser().feed(raw)
    assert len(packets) == 1
    assert packets[0].command == Command.PING
    assert packets[0].sequence == 7
    assert packets[0].payload == b""


def test_parser_ignores_garbage_before_magic():
    raw = b"noise" + build_packet(Command.PING, 1)
    packets = PacketParser().feed(raw)
    assert len(packets) == 1
    assert packets[0].sequence == 1


def test_parser_handles_fragmented_packet():
    raw = build_packet(Command.GET_INFO, 42, b"abc")
    parser = PacketParser()
    assert parser.feed(raw[:3]) == []
    assert parser.feed(raw[3:8]) == []
    packets = parser.feed(raw[8:])
    assert len(packets) == 1
    assert packets[0].payload == b"abc"


def test_parser_handles_back_to_back_packets():
    raw = build_packet(Command.PING, 1) + build_packet(Command.REBOOT, 2)
    packets = PacketParser().feed(raw)
    assert [packet.sequence for packet in packets] == [1, 2]


def test_parser_rejects_bad_crc():
    raw = bytearray(build_packet(Command.PING, 1))
    raw[-1] ^= 0xFF
    assert PacketParser().feed(bytes(raw)) == []


def test_parse_info_payload():
    strings = [b"ESP32", b"ota_0", b"v5.3", b"esp32_serial_ota", b"Aug 29 2026"]
    payload = struct.pack("<BBBBIII", 1, 0, 1, 0, 123, 456, 7)
    payload += b"".join(bytes([len(value)]) + value for value in strings)

    info = parse_info_payload(payload)

    assert info["protocol_version"] == 1
    assert info["firmware"] == "0.1.0"
    assert info["boot_count"] == 7
    assert info["chip_model"] == "ESP32"
    assert info["running_partition"] == "ota_0"


def test_build_ota_begin_payload():
    payload = build_ota_begin_payload(1234, bytes(range(32)))
    firmware_size, sha256 = struct.unpack("<I32s", payload)
    assert firmware_size == 1234
    assert sha256 == bytes(range(32))


def test_build_ota_data_payload():
    payload = build_ota_data_payload(2, 1016, b"abc")
    chunk_number, offset = struct.unpack("<II", payload[:8])
    assert chunk_number == 2
    assert offset == 1016
    assert payload[8:] == b"abc"


def test_ota_chunk_data_size_fits_protocol_payload():
    payload = build_ota_data_payload(0, 0, b"x" * OTA_CHUNK_DATA_SIZE)
    assert len(payload) == 1024
