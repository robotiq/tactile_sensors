"""Talking to the hub application: the packet parser and the hardware revision detection."""

import pytest

from conftest import bh, device_info, stream_packet


def test_reply_is_found(serial_device):
    assert bh.request_usb_reply(serial_device, bh.USB_COMMAND_GET_FIRMWARE_VERSION) == b"2.2.0\x00"


def test_reply_is_found_behind_stream_packets(hub, serial_device):
    hub.before_reply = stream_packet() * 3

    assert bh.request_usb_reply(serial_device, bh.USB_COMMAND_GET_FIRMWARE_VERSION) == b"2.2.0\x00"


def test_reply_is_found_behind_a_cut_stream_packet_holding_a_start_byte(hub, serial_device):
    # The flush cut a stream packet: its tail holds 0x9A followed by a large "length" that would swallow the reply
    hub.flush_leftover = bytes([0x11, bh.USB_PACKET_START_BYTE, 0x00, 0x58, 0xFF, 0x01])

    assert bh.request_usb_reply(serial_device, bh.USB_COMMAND_GET_FIRMWARE_VERSION) == b"2.2.0\x00"


def test_reply_with_a_non_zero_crc_byte_is_found(hub, serial_device):
    # Firmware up to 1.1.3 sends whatever was on its stack as the CRC byte
    hub.reply_crc = 0xA7

    assert bh.request_usb_reply(serial_device, bh.USB_COMMAND_GET_FIRMWARE_VERSION) == b"2.2.0\x00"


def test_no_reply_times_out(hub, serial_device):
    hub.version = None

    assert bh.request_usb_reply(serial_device, bh.USB_COMMAND_GET_FIRMWARE_VERSION) is None


def test_drain_input_discards_what_is_waiting(hub, serial_device):
    serial_device.buffer += stream_packet() * 10

    bh.drain_input(serial_device)

    assert serial_device.in_waiting == 0


@pytest.mark.parametrize("info, status", [
    (device_info(), "valid"),
    (None, "blank"),
    (device_info()[:-1], "blank"),
    (device_info(valid_checksum=False), "blank"),
    (device_info(hardware_type=0x04), "inconsistent"),
    (device_info(serial_number=b"TP222015"), "inconsistent"),
], ids=["valid", "no reply", "short reply", "bad checksum", "not a hub type", "not a hub serial number"])
def test_read_hub_device_info(hub, serial_device, info, status):
    hub.device_info = info

    assert bh.read_hub_device_info(serial_device)[0] == status


@pytest.mark.parametrize("old_vid_pid, version, info, hw_rev", [
    (True, b"1.1.8\x00", None, "2.1"),
    (True, None, None, "2.1"),
    (False, b"1.1.8\x00", None, "2.1"),
    (False, b"2.2.0\x00", device_info(), "2.2"),
    (False, b"2.2.0\x00", None, "2.2"),
    (False, b"2.2.0\x00", device_info(hardware_type=0x04), None),
    (False, None, None, None),
    (False, b"3.0.0\x00", None, None),
    (False, b"garbage\x00", None, None),
], ids=["old VID/PID", "old VID/PID, no version reply", "1.x", "2.x, valid EEPROM", "2.x, blank EEPROM",
        "2.x, unexpected EEPROM", "no version reply", "unknown family", "unparseable version"])
def test_detect_hardware_revision(hub, serial_device, old_vid_pid, version, info, hw_rev):
    hub.version, hub.device_info = version, info

    assert bh.detect_hardware_revision(serial_device, old_vid_pid) == hw_rev


def test_detection_stops_the_sensor_stream_first(hub, serial_device):
    bh.detect_hardware_revision(serial_device, False)

    assert hub.serial_commands[0] == bh.USB_COMMAND_AUTOSEND_ASYNC_SENSORS
