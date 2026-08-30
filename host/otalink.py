from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shlex
import sys
import time

from .protocol import (
    Command,
    ErrorCode,
    Packet,
    PacketParser,
    OTA_CHUNK_DATA_SIZE,
    build_ota_data_payload,
    build_ota_begin_payload,
    build_packet,
    parse_info_payload,
    ProtocolError,
)

SERIAL_READ_TIMEOUT = 0.02


class SerialTransport:
    def __init__(self, port: str, baud: int, timeout: float) -> None:
        try:
            import serial
        except ImportError as exc:
            raise SystemExit(
                "pyserial is required. Install it with: python3 -m pip install pyserial"
            ) from exc

        self._serial = serial.Serial()
        self._serial.port = port
        self._serial.baudrate = baud
        self._serial.timeout = SERIAL_READ_TIMEOUT
        self._serial.rtscts = False
        self._serial.dsrdtr = False
        self._serial.dtr = False
        self._serial.rts = False
        self._serial.open()
        self._serial.reset_input_buffer()
        self._serial.reset_output_buffer()
        self._parser = PacketParser()

    def request(self, packet: bytes, sequence: int, timeout: float) -> Packet:
        self._serial.write(packet)
        self._serial.flush()
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            available = self._serial.in_waiting
            chunk = self._serial.read(available or 1)
            if not chunk:
                continue
            for parsed in self._parser.feed(chunk):
                if parsed.sequence == sequence:
                    return parsed

        raise TimeoutError("timed out waiting for response")

    def close(self) -> None:
        self._serial.close()


class EspCtlClient:
    def __init__(self, port: str, baud: int, timeout: float, retries: int) -> None:
        self._transport = SerialTransport(port, baud, timeout)
        self._timeout = timeout
        self._retries = retries
        self._sequence = int(time.monotonic() * 1000) & 0xFFFF

    def close(self) -> None:
        self._transport.close()

    def request(
        self,
        command: Command,
        payload: bytes = b"",
        timeout: float | None = None,
    ) -> Packet:
        for attempt in range(1, self._retries + 1):
            sequence = self._next_sequence()
            try:
                return self._transport.request(
                    build_packet(command, sequence, payload),
                    sequence,
                    timeout or self._timeout,
                )
            except TimeoutError:
                if attempt == self._retries:
                    raise

        raise TimeoutError("timed out waiting for response")

    def _next_sequence(self) -> int:
        self._sequence = (self._sequence + 1) & 0xFFFF
        return self._sequence


def open_client(args: argparse.Namespace) -> EspCtlClient:
    return EspCtlClient(args.port, args.baud, args.timeout, args.retries)


def ensure_ack(packet: Packet, context: str = "command") -> None:
    if packet.command == Command.ACK:
        status = packet.payload[0] if packet.payload else ErrorCode.OK
        if status == ErrorCode.OK:
            return
        raise SystemExit(f"Device returned ACK status {status} during {context}")

    if packet.command == Command.NACK:
        code = packet.payload[0] if packet.payload else ErrorCode.INVALID_PACKET
        name = ErrorCode(code).name if code in ErrorCode._value2member_map_ else f"0x{code:02X}"
        raise SystemExit(f"Device rejected {context}: {name}")

    raise SystemExit(f"Unexpected response command during {context}: 0x{packet.command:02X}")


def run_ping(client: EspCtlClient, port: str) -> None:
    try:
        packet = client.request(Command.PING)
    except TimeoutError as exc:
        raise SystemExit(f"No response from device on {port}") from exc

    ensure_ack(packet, "PING")
    print("Device responded")


def run_info(client: EspCtlClient, port: str, timeout: float) -> None:
    try:
        packet = client.request(Command.GET_INFO, timeout=max(timeout, 3.0))
    except TimeoutError as exc:
        raise SystemExit(f"No GET_INFO response from device on {port}") from exc

    if packet.command == Command.NACK:
        ensure_ack(packet, "GET_INFO")
    if packet.command != Command.INFO:
        raise SystemExit(f"Unexpected response command: 0x{packet.command:02X}")

    try:
        info = parse_info_payload(packet.payload)
    except ProtocolError as exc:
        raise SystemExit(f"Could not parse GET_INFO response: {exc}") from exc
    print(f"Project:           {info['project_name']}")
    print(f"Firmware:          {info['firmware']}")
    print(f"Protocol:          {info['protocol_version']}")
    print(f"Chip:              {info['chip_model']}")
    print(f"ESP-IDF:           {info['idf_version']}")
    print(f"Running partition: {info['running_partition']}")
    print(f"Boot partition:    {info['boot_partition']}")
    print(f"OTA state:         {info['ota_state']}")
    if info["ota_slots"]:
        print("OTA slots:")
        for slot in info["ota_slots"]:
            print(f"  {slot['label']}: {slot['state']}")
    print(f"Rollback possible: {'yes' if info['rollback_possible'] else 'no'}")
    print(f"Free heap:         {info['free_heap']} bytes")
    print(f"Boot count:        {info['boot_count']}")
    print(f"Uptime:            {info['uptime_seconds']} seconds")
    print(f"Build date:        {info['build_date']}")


def run_reboot(client: EspCtlClient, port: str) -> None:
    try:
        packet = client.request(Command.REBOOT)
    except TimeoutError as exc:
        raise SystemExit(f"No response from device on {port}") from exc

    ensure_ack(packet, "REBOOT")
    print("Reboot requested")


def run_rollback(client: EspCtlClient, port: str) -> None:
    try:
        packet = client.request(Command.ROLLBACK)
    except TimeoutError as exc:
        raise SystemExit(f"No ROLLBACK response from device on {port}") from exc

    ensure_ack(packet, "ROLLBACK")
    print("Rollback requested")


def firmware_metadata(path: Path) -> tuple[int, bytes, bytes]:
    size = path.stat().st_size
    if size < 32:
        raise SystemExit(f"Firmware file is too small to be an ESP-IDF image: {path}")

    digest = hashlib.sha256()
    with path.open("rb") as firmware:
        for chunk in iter(lambda: firmware.read(64 * 1024), b""):
            digest.update(chunk)

    with path.open("rb") as firmware:
        firmware.seek(-32, 2)
        image_digest = firmware.read(32)

    return size, digest.digest(), image_digest


def run_update_begin(client: EspCtlClient, port: str, firmware_path: Path) -> None:
    if not firmware_path.is_file():
        raise SystemExit(f"Firmware file not found: {firmware_path}")

    size, file_sha256, image_sha256 = firmware_metadata(firmware_path)
    print(f"Firmware:          {firmware_path}")
    print(f"Size:              {size} bytes")
    print(f"File SHA256:       {file_sha256.hex()}")
    print(f"Image SHA256:      {image_sha256.hex()}")

    payload = build_ota_begin_payload(size, image_sha256)
    try:
        packet = client.request(Command.OTA_BEGIN, payload, timeout=5.0)
    except TimeoutError as exc:
        raise SystemExit(f"No OTA_BEGIN response from device on {port}") from exc

    ensure_ack(packet, "OTA_BEGIN")
    print("OTA begin accepted")


def run_update_data(client: EspCtlClient, port: str, firmware_path: Path, size: int) -> None:
    sent = 0
    chunk_number = 0

    with firmware_path.open("rb") as firmware:
        while True:
            chunk = firmware.read(OTA_CHUNK_DATA_SIZE)
            if not chunk:
                break

            payload = build_ota_data_payload(chunk_number, sent, chunk)
            try:
                packet = client.request(Command.OTA_DATA, payload, timeout=5.0)
            except TimeoutError as exc:
                raise SystemExit(
                    f"No OTA_DATA response from device on {port} at offset {sent}"
                ) from exc

            ensure_ack(packet, f"OTA_DATA chunk={chunk_number} offset={sent}")
            sent += len(chunk)
            chunk_number += 1
            percent = (sent * 100) // size
            print(f"\rUploading:         {percent:3d}% ({sent}/{size} bytes)", end="")

    print()
    print("OTA data transferred")


def run_update_end(client: EspCtlClient, port: str) -> None:
    try:
        packet = client.request(Command.OTA_END, timeout=10.0)
    except TimeoutError as exc:
        raise SystemExit(f"No OTA_END response from device on {port}") from exc

    ensure_ack(packet, "OTA_END")
    print("OTA finalized")


def run_abort(client: EspCtlClient, port: str) -> None:
    try:
        packet = client.request(Command.OTA_ABORT)
    except TimeoutError as exc:
        raise SystemExit(f"No OTA_ABORT response from device on {port}") from exc

    ensure_ack(packet, "OTA_ABORT")
    print("OTA aborted")


def cmd_ping(args: argparse.Namespace) -> int:
    client = open_client(args)
    try:
        run_ping(client, args.port)
    finally:
        client.close()
    return 0


def cmd_info(args: argparse.Namespace) -> int:
    client = open_client(args)
    try:
        run_info(client, args.port, args.timeout)
    finally:
        client.close()
    return 0


def cmd_reboot(args: argparse.Namespace) -> int:
    client = open_client(args)
    try:
        run_reboot(client, args.port)
    finally:
        client.close()
    return 0


def cmd_rollback(args: argparse.Namespace) -> int:
    client = open_client(args)
    try:
        run_rollback(client, args.port)
    finally:
        client.close()
    return 0


def cmd_update(args: argparse.Namespace) -> int:
    client = open_client(args)
    try:
        firmware_path = Path(args.firmware)
        if not firmware_path.is_file():
            raise SystemExit(f"Firmware file not found: {firmware_path}")
        size, _file_sha256, _image_sha256 = firmware_metadata(firmware_path)
        run_update_begin(client, args.port, firmware_path)
        run_update_data(client, args.port, firmware_path, size)
        run_update_end(client, args.port)
        print("Run 'reboot' to boot the finalized image.")
    finally:
        client.close()
    return 0


def cmd_abort(args: argparse.Namespace) -> int:
    client = open_client(args)
    try:
        run_abort(client, args.port)
    finally:
        client.close()
    return 0


def cmd_shell(args: argparse.Namespace) -> int:
    client = open_client(args)
    print(f"Connected to {args.port}. Commands: ping, info, reboot, rollback, update <bin>, end, abort, quit")
    try:
        while True:
            try:
                line = input("espctl> ")
            except EOFError:
                print()
                break

            parts = shlex.split(line)
            if not parts:
                continue

            command = parts[0].lower()
            try:
                if command in {"quit", "exit"}:
                    break
                if command == "ping":
                    run_ping(client, args.port)
                elif command == "info":
                    run_info(client, args.port, args.timeout)
                elif command == "reboot":
                    run_reboot(client, args.port)
                elif command == "rollback":
                    run_rollback(client, args.port)
                elif command == "update":
                    if len(parts) != 2:
                        print("Usage: update <firmware.bin>")
                        continue
                    firmware_path = Path(parts[1])
                    if not firmware_path.is_file():
                        print(f"Firmware file not found: {firmware_path}")
                        continue
                    size, _file_sha256, _image_sha256 = firmware_metadata(firmware_path)
                    run_update_begin(client, args.port, firmware_path)
                    run_update_data(client, args.port, firmware_path, size)
                    run_update_end(client, args.port)
                    print("Run 'reboot' to boot the finalized image.")
                elif command == "end":
                    run_update_end(client, args.port)
                elif command == "abort":
                    run_abort(client, args.port)
                else:
                    print(f"Unknown command: {command}")
            except SystemExit as exc:
                if exc.code:
                    print(exc.code)
    finally:
        client.close()

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="espctl")
    parser.add_argument("--port", required=True, help="Serial device such as /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--retries", type=int, default=3)

    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("ping").set_defaults(func=cmd_ping)
    subparsers.add_parser("info").set_defaults(func=cmd_info)
    subparsers.add_parser("reboot").set_defaults(func=cmd_reboot)
    subparsers.add_parser("rollback").set_defaults(func=cmd_rollback)
    update_parser = subparsers.add_parser("update")
    update_parser.add_argument("firmware", help="Firmware .bin file")
    update_parser.set_defaults(func=cmd_update)
    subparsers.add_parser("abort").set_defaults(func=cmd_abort)
    subparsers.add_parser("shell").set_defaults(func=cmd_shell)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
