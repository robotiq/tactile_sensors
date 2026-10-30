"""The whole script against a simulated hub: what gets flashed, and every case where nothing may be flashed."""

import shutil
import sys

import pytest

from conftest import BIN_DIR, bh, device_info, firmware_rows

FW_1 = str(BIN_DIR / "Master_hub_1.1.8.cyacd")
FW_2 = str(BIN_DIR / "Master_hub_2.2.0.cyacd")


def on_2_1_hub(hub):
    hub.version, hub.device_info = b"1.1.8\x00", None


def on_old_vid_pid_hub(hub):
    on_2_1_hub(hub)
    hub.old_vid_pid = True


def no_version_reply(hub):
    hub.version = None


def assert_flashed(hub, firmware):
    assert hub.rows == firmware_rows(firmware)
    assert hub.exited_bootloader


def assert_nothing_flashed(hub):
    assert not hub.rebooted
    assert not hub.rows


@pytest.mark.parametrize("setup, argv, typed, firmware", [
    (None, [], "", FW_2),
    (on_2_1_hub, [], "", FW_1),
    (on_old_vid_pid_hub, [], "", FW_1),
    (None, ["--latest"], "", FW_2),
    (None, ["--hw-rev", "2.2"], "", FW_2),
    (None, ["--firmware", FW_2], "", FW_2),
    (on_2_1_hub, ["--firmware", FW_1], "", FW_1),
    (lambda hub: setattr(hub, "device_info", None), [], "", FW_2),
    (None, ["--hw-rev", "2.1", "--force"], "FORCE\n", FW_1),
    (no_version_reply, ["--hw-rev", "2.2", "--force"], "FORCE\n", FW_2),
    (None, ["--firmware", FW_1, "--force"], "FORCE\n", FW_1),
    (lambda hub: hub.start_in_bootloader(), ["--hw-rev", "2.2"], "", FW_2),
    (lambda hub: hub.start_in_bootloader(), ["--hw-rev", "2.1"], "", FW_1),
], ids=["2.2 auto", "2.1 auto", "old VID/PID auto", "--latest", "matching --hw-rev", "matching --firmware 2.x",
        "matching --firmware 1.x", "2.2 with blank EEPROM", "--hw-rev overrides detection after FORCE",
        "--hw-rev after failed detection and FORCE", "other family --firmware after FORCE",
        "in bootloader, --hw-rev 2.2", "in bootloader, --hw-rev 2.1"])
def test_flashes(hub, run, setup, argv, typed, firmware):
    if setup:
        setup(hub)

    code, err = run(*argv, typed=typed)

    assert code == 0, err
    assert "Device programmed successfully!" in err
    assert_flashed(hub, firmware)


@pytest.mark.parametrize("setup, argv, typed, message", [
    (None, ["--firmware", FW_1], "", "Firmware 1.1.8 is not made for hardware revision 2.2"),
    (on_2_1_hub, ["--firmware", FW_2], "", "Firmware 2.2.0 is not made for hardware revision 2.1"),
    (on_old_vid_pid_hub, ["--firmware", FW_2], "", "Firmware 2.2.0 is not made for hardware revision 2.1"),
    (None, ["--hw-rev", "2.1"], "", "--hw-rev 2.1 doesn't match the detected hardware revision 2.2"),
    (None, ["--hw-rev", "2.1", "--force"], "no\n", "Aborted"),
    (None, ["--hw-rev", "2.1", "--force"], "", "Aborted"),
    (no_version_reply, [], "", "Unknown hardware revision"),
    (no_version_reply, ["--hw-rev", "2.2"], "", "couldn't be detected, so --hw-rev 2.2 can't be checked"),
    (no_version_reply, ["--firmware", FW_2], "", "The hardware revision is unknown"),
    (lambda hub: setattr(hub, "device_info", device_info(hardware_type=0x04)), [], "", "unexpected device info"),
    (lambda hub: setattr(hub, "version", b"3.0.0\x00"), [], "", "Unknown firmware family 3.0.0"),
    (lambda hub: hub.start_in_bootloader(), [], "", "Unknown hardware revision"),
    (lambda hub: setattr(hub, "app_running", False), ["--firmware", FW_2], "", "The hardware revision is unknown"),
], ids=["1.x --firmware on 2.2", "2.x --firmware on 2.1", "2.x --firmware on old VID/PID",
        "contradicting --hw-rev", "FORCE not typed", "no input for FORCE", "no version reply",
        "--hw-rev after failed detection", "--firmware with unknown revision", "unexpected EEPROM",
        "unknown family", "in bootloader without --hw-rev", "--firmware with no hub detected"])
def test_refuses_before_rebooting(hub, run, setup, argv, typed, message):
    if setup:
        setup(hub)

    code, err = run(*argv, typed=typed)

    assert code == 1
    assert message in err
    assert_nothing_flashed(hub)


def test_refuses_a_firmware_file_name_without_version(hub, run, tmp_path):
    firmware = tmp_path / "hub_firmware.cyacd"
    shutil.copy(FW_2, firmware)

    code, err = run("--firmware", str(firmware))

    assert code == 1
    assert "can't be read from the file name" in err
    assert_nothing_flashed(hub)


def test_refuses_a_missing_firmware_file_before_rebooting(hub, run, tmp_path):
    code, err = run("--firmware", str(tmp_path / "Master_hub_2.2.0.cyacd"))

    assert code == 1
    assert "Can't read firmware file" in err
    assert_nothing_flashed(hub)


def test_refuses_a_corrupt_firmware_file_before_rebooting(hub, run, tmp_path):
    firmware = tmp_path / "Master_hub_2.2.0.cyacd"
    firmware.write_text(open(FW_2).read()[:-40])

    code, err = run("--firmware", str(firmware))

    assert code == 1
    assert "Can't read firmware file" in err
    assert_nothing_flashed(hub)


def test_refuses_firmware_for_another_chip_before_rebooting(hub, run, tmp_path):
    lines = open(FW_2).read().splitlines(keepends=True)
    firmware = tmp_path / "Master_hub_2.2.0.cyacd"
    firmware.write_text("FF" + lines[0][2:] + "".join(lines[1:]))

    code, err = run("--firmware", str(firmware))

    assert code == 1
    assert "Firmware is for silicon 0xFF" in err
    assert_nothing_flashed(hub)


def test_bootloader_that_never_appears_points_to_hw_rev(hub, run):
    hub.reset_delay = None

    code, err = run()

    assert code == 1
    assert "Bootloader not found after 30 seconds" in err
    assert "run again with --hw-rev 2.2" in err
    assert not hub.rows


def test_bootloader_that_cannot_be_opened_fails(hub, run):
    hub.hid_open_error = True

    code, err = run()

    assert code == 1
    assert "open failed" in err
    assert not hub.rows


def test_invalid_application_checksum_fails(hub, run):
    hub.app_checksum_valid = False

    code, err = run()

    assert code == 1
    assert "Application checksum is invalid" in err
    assert not hub.exited_bootloader


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="the root warning is Linux only")
@pytest.mark.parametrize("euid, warned", [(1000, True), (0, False)])
def test_warns_when_not_root_on_linux(hub, run, monkeypatch, euid, warned):
    monkeypatch.setattr(bh.os, "geteuid", lambda: euid)

    code, err = run()

    assert code == 0
    assert ("Not running as root" in err) == warned
