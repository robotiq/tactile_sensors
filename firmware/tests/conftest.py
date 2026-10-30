"""A simulated Master Hub for bootloader_host.py: its serial application, then its HID bootloader after the reboot
command, on a fake clock so the bootloader wait runs instantly. No hardware, hidapi or pyserial needed."""

import importlib.util
import io
import sys
import types
from pathlib import Path
from unittest import mock

import pytest

FIRMWARE_DIR = Path(__file__).resolve().parent.parent
BIN_DIR = FIRMWARE_DIR / "bin"

# bootloader_host imports hid and serial at load time: give it stubs, the fixtures below swap in the fakes
with mock.patch.dict(sys.modules, {name: types.ModuleType(name)
                                   for name in ("hid", "serial", "serial.tools", "serial.tools.list_ports")}):
    _spec = importlib.util.spec_from_file_location("bootloader_host", FIRMWARE_DIR / "bootloader_host.py")
    bh = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(bh)


def device_info(hardware_type=0x03, serial_number=b"TH100031", valid_checksum=True):
    """The 0xE4 reply: a hub row, then two finger rows."""
    def row(row_type, serial):
        data = bytearray([bh.DEVICE_INFO_ID, row_type, 2, 2, 0]) + serial + b"\x1a\x07"
        data.append((sum(data) + (0 if valid_checksum else 1)) & 0xFF)
        return bytes(data)
    return row(hardware_type, serial_number) + row(0x04, b"TP222015") + row(0x04, b"TP222016")


def stream_packet(payload=b"\x01\x02\x03\x04"):
    return bytes([bh.USB_PACKET_START_BYTE, 0x00, 0x58, len(payload)]) + payload


class FakeClock:
    def __init__(self):
        self.now = 1000.0

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class Hub:
    """State of the simulated hub. Tests change the attributes before running the script."""

    def __init__(self, clock):
        self.clock = clock
        self.app_running = True
        self.old_vid_pid = False
        self.version = b"2.2.0\x00"      # 0xE3 reply data, None for no reply
        self.device_info = device_info()  # 0xE4 reply data, None for no reply
        self.flush_leftover = b""         # bytes the input buffer still holds right after a flush
        self.before_reply = b""           # bytes sent ahead of each reply
        self.reset_delay = 22.0           # seconds from 0xE2 to the bootloader enumerating, None for never
        self.bootloader_at = None
        self.hid_open_error = False
        self.app_checksum_valid = True
        self.serial_commands = []
        self.pending_data = b""
        self.rows = []                    # (array_id, row_number, data) programmed in the bootloader
        self.hid_packets = []
        self.exited_bootloader = False

    def start_in_bootloader(self):
        self.app_running = False
        self.bootloader_at = self.clock.now

    @property
    def rebooted(self):
        return bh.USB_COMMAND_ENTER_BOOTLOADER in self.serial_commands


class FakeSerial:
    def __init__(self, hub, port):
        self.hub = hub
        self.port = port
        self.is_open = True
        self.timeout = 1
        self.buffer = bytearray()

    @property
    def in_waiting(self):
        return len(self.buffer)

    def read(self, size):
        if not self.buffer:
            self.hub.clock.sleep(self.timeout)
        data = bytes(self.buffer[:size])
        del self.buffer[:size]
        return data

    def reset_input_buffer(self):
        self.buffer = bytearray(self.hub.flush_leftover)

    def write(self, packet):
        command = packet[2]
        self.hub.serial_commands.append(command)
        reply = {bh.USB_COMMAND_GET_FIRMWARE_VERSION: self.hub.version,
                 bh.USB_COMMAND_GET_DEVICES_INFO: self.hub.device_info}.get(command)
        if reply is not None:
            self.buffer += self.hub.before_reply
            self.buffer += bytes([bh.USB_PACKET_START_BYTE, 0x00, command, len(reply)]) + reply
        if command == bh.USB_COMMAND_ENTER_BOOTLOADER and self.hub.reset_delay is not None:
            self.hub.bootloader_at = self.hub.clock.now + self.hub.reset_delay

    def close(self):
        self.is_open = False


class FakeHidDevice:
    """Answers the Cypress bootloader protocol: 0x38 enter, 0x37 send data, 0x39 program row, 0x31 verify, 0x3b exit."""

    def __init__(self, hub):
        self.hub = hub
        self.response = None

    def open(self, vid, pid):
        if self.hub.hid_open_error:
            raise OSError("open failed")

    def write(self, packet):
        packet = bytes(packet)
        self.hub.hid_packets.append(packet)
        command, length = packet[2], packet[3] | (packet[4] << 8)
        data = packet[5:5 + length]
        reply = b""
        if command == 0x38:
            reply = bh.JTAG_ID.to_bytes(4, "little") + bytes([bh.DEVICE_REV, 0, 0, 1])
        elif command == 0x37:
            self.hub.pending_data += data
        elif command == 0x39:
            self.hub.rows.append((data[0], data[1] | (data[2] << 8), self.hub.pending_data + data[3:]))
            self.hub.pending_data = b""
        elif command == 0x31:
            reply = bytes([1 if self.hub.app_checksum_valid else 0])
        elif command == 0x3B:
            self.hub.exited_bootloader = True
            return
        response = bytearray([0x01, 0x00, len(reply), 0]) + reply
        checksum = (~sum(response) + 1) & 0xFFFF
        self.response = list(response + bytes([checksum & 0xFF, checksum >> 8, 0x17]))

    def read(self, size):
        return self.response


@pytest.fixture
def clock(monkeypatch):
    clock = FakeClock()
    monkeypatch.setattr(bh, "time", clock)
    return clock


@pytest.fixture
def hub(monkeypatch, clock):
    hub = Hub(clock)

    def comports():
        if not hub.app_running:
            return []
        vid, pid = ((bh.MASTER_HUB_APP_VID_OLD, bh.MASTER_HUB_APP_PID_OLD) if hub.old_vid_pid
                    else (bh.MASTER_HUB_APP_VID, bh.MASTER_HUB_APP_PID))
        return [types.SimpleNamespace(vid=vid, pid=pid, device="/dev/ttyACM0")]

    serial = types.SimpleNamespace(
        EIGHTBITS=8, PARITY_NONE="N", STOPBITS_ONE=1,
        Serial=lambda port, **settings: FakeSerial(hub, port),
        tools=types.SimpleNamespace(list_ports=types.SimpleNamespace(comports=comports)))

    def enumerate_hid():
        if hub.bootloader_at is not None and clock.now >= hub.bootloader_at:
            return [{"vendor_id": bh.BOOTLOADER_VID, "product_id": bh.BOOTLOADER_PID}]
        return []

    monkeypatch.setattr(bh, "serial", serial)
    monkeypatch.setattr(bh, "hid", types.SimpleNamespace(enumerate=enumerate_hid, device=lambda: FakeHidDevice(hub)))
    return hub


@pytest.fixture
def serial_device(hub):
    device = FakeSerial(hub, "/dev/ttyACM0")
    device.timeout = 0.1
    return device


@pytest.fixture
def run(monkeypatch, capsys, hub):
    """Runs the script with the given arguments and typed input, returns (exit code, stderr)."""
    def run(*argv, typed=""):
        monkeypatch.setattr(sys, "argv", ["bootloader_host.py", *argv])
        monkeypatch.setattr(sys, "stdin", io.StringIO(typed))
        try:
            bh.main()
            code = 0
        except SystemExit as error:
            code = error.code
        return code, capsys.readouterr().err
    return run


def firmware_rows(path):
    with open(path) as firmware_file:
        firmware = bh.Cyacd(firmware_file)
        firmware.parse()
    return firmware.firmware
