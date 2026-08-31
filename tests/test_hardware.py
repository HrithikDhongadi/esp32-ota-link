import os
import subprocess
import sys
import time
from pathlib import Path

import pytest

from host.otalink import OtaLinkClient, ensure_ack, firmware_metadata, parse_auth_key
from host.protocol import (
    Command,
    OTA_CHUNK_DATA_SIZE,
    build_ota_begin_payload,
    build_ota_data_payload,
)


pytestmark = pytest.mark.hardware


def _port() -> str:
    port = os.environ.get("OTALINK_PORT")
    if not port:
        pytest.skip("set OTALINK_PORT to run hardware tests")
    if not Path(port).exists():
        pytest.skip(f"{port} is not available")
    return port


def _run_otalink(*args: str) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, "-m", "host.otalink", "--port", _port()]
    auth_key = os.environ.get("OTALINK_AUTH_KEY")
    if auth_key:
        command += ["--auth-key", auth_key]
    command += list(args)

    result = subprocess.run(
        command,
        cwd=Path(__file__).resolve().parents[1],
        text=True,
        capture_output=True,
        timeout=60,
    )
    assert result.returncode == 0, (
        f"otalink {' '.join(args)} failed\n"
        f"stdout:\n{result.stdout}\n"
        f"stderr:\n{result.stderr}"
    )
    return result


def _firmware() -> str:
    firmware = os.environ.get("OTALINK_FIRMWARE")
    if not firmware:
        pytest.skip("set OTALINK_FIRMWARE to run OTA transfer hardware tests")
    return firmware


def test_hardware_ping():
    result = _run_otalink("ping")

    assert "Device responded" in result.stdout


def test_hardware_info_reports_esp32():
    result = _run_otalink("info")

    assert "Project:" in result.stdout
    assert "Firmware:" in result.stdout
    assert "Chip:" in result.stdout


def test_hardware_interrupted_update_can_abort():
    firmware = _firmware()
    size, _file_sha256, image_sha256 = firmware_metadata(Path(firmware))
    auth_key = os.environ.get("OTALINK_AUTH_KEY")
    client = OtaLinkClient(
        _port(),
        115200,
        1.0,
        3,
        auth_key=parse_auth_key(auth_key),
    )
    try:
        begin = client.request(
            Command.OTA_BEGIN,
            build_ota_begin_payload(size, image_sha256),
            timeout=5.0,
        )
        ensure_ack(begin, "OTA_BEGIN")

        with Path(firmware).open("rb") as image:
            chunk = image.read(OTA_CHUNK_DATA_SIZE)
        data = client.request(
            Command.OTA_DATA,
            build_ota_data_payload(0, 0, chunk),
            timeout=5.0,
        )
        ensure_ack(data, "OTA_DATA")

        abort = client.request(Command.OTA_ABORT)
        ensure_ack(abort, "OTA_ABORT")
    finally:
        client.close()

    info = _run_otalink("info")
    assert "Project:" in info.stdout


def test_hardware_update_reboot_and_validate_when_firmware_path_is_set():
    firmware = _firmware()

    update = _run_otalink("update", firmware)
    assert "OTA finalized" in update.stdout

    reboot = _run_otalink("reboot")
    assert "Reboot requested" in reboot.stdout

    time.sleep(8)
    info = _run_otalink("info")

    assert "OTA state:         VALID" in info.stdout
    assert "Running partition:" in info.stdout
    assert "Boot partition:" in info.stdout
