"""The HID bootloader: waiting for it after the reboot, and flashing rows in packets it accepts."""

import pytest

from conftest import BIN_DIR, bh, firmware_rows


def test_wait_finds_a_bootloader_that_takes_22_seconds(hub, clock):
    hub.bootloader_at = clock.now + 22

    assert bh.wait_for_bootloader(bh.BOOTLOADER_VID, bh.BOOTLOADER_PID)


def test_wait_gives_up_after_the_timeout(hub, clock):
    start = clock.now

    assert not bh.wait_for_bootloader(bh.BOOTLOADER_VID, bh.BOOTLOADER_PID)
    assert clock.now - start == pytest.approx(bh.BOOTLOADER_WAIT_TIMEOUT, abs=1)


class Firmware:
    def __init__(self, rows):
        self.firmware = rows


@pytest.mark.parametrize("length", [1, 47, 48, 49, 96, 100, 256])
def test_flash_splits_a_row_into_packets_and_programs_it_whole(hub, length):
    row = (0, 7, bytes(i & 0xFF for i in range(length)))

    assert bh.Bootloader(bh.BOOTLOADER_VID, bh.BOOTLOADER_PID).flash(Firmware([row]))
    assert hub.rows == [row]
    assert all(len(packet) <= 65 for packet in hub.hid_packets)  # report ID + one 64-byte HID report


def test_flash_programs_every_row_of_the_shipped_firmware(hub):
    rows = firmware_rows(BIN_DIR / "Master_hub_2.2.0.cyacd")

    assert bh.Bootloader(bh.BOOTLOADER_VID, bh.BOOTLOADER_PID).flash(Firmware(rows))
    assert hub.rows == rows
