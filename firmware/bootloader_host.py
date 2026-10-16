#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2016  Forest Crossman <cyrozap@gmail.com>
# Copyright (C) 2026  Robotiq
#
# Based on cyrozap/Cypress-HID-Bootloader-Host; modified 2026 by Robotiq for
# Master Hub integration (device VID/PIDs, firmware selection, the serial
# reboot-to-bootloader handshake, the hardware revision compatibility check,
# and the application checksum verification after programming).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# "04b4:f13b" "../Master_hub.cydsn/CortexM3/ARM_GCC_541/Release/Master_hub.cyacd"

import argparse
import binascii
import sys

import hid  # requires the 'hidapi' package (pip install hidapi), works on Windows, Linux, and macOS

import serial
import serial.tools.list_ports

import time
import os
import glob
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BIN_DIR = os.path.join(SCRIPT_DIR, "bin")

BOOTLOADER_VID = 0x04B4
BOOTLOADER_PID = 0xB71D

MASTER_HUB_APP_VID_OLD = 0x04B4
MASTER_HUB_APP_PID_OLD = 0xF232

MASTER_HUB_APP_VID = 0x16D0
MASTER_HUB_APP_PID = 0x14CC

# Master Hub application USB protocol (see Master_hub.cydsn/usb_comm.h)
USB_PACKET_START_BYTE = 0x9A
USB_COMMAND_AUTOSEND_ASYNC_SENSORS = 0x59
USB_COMMAND_ENTER_BOOTLOADER = 0xE2
USB_COMMAND_GET_FIRMWARE_VERSION = 0xE3  # Answered by every firmware released
USB_COMMAND_GET_DEVICES_INFO = 0xE4      # Firmware 2.1.0 and later only

# Device info rows (EEA_row_struct): id, hardware_version[4], serial_number[8], production_year[2], checksum
DEVICE_INFO_ROW_SIZE = 16
DEVICE_INFO_ROW_COUNT = 3  # Hub, finger 1, finger 2
DEVICE_INFO_ID = 0x01
DEVICE_INFO_HUB_TYPE = 0x03
DEVICE_INFO_HUB_SERIAL_PREFIX = b"TH1"

# The hub and its fingers always share the same hardware revision, and each revision runs its own
# firmware family (major version). Finger serial numbers start with 21xxx on 2.1 and 22xxx on 2.2.
FIRMWARE_FAMILY = {
    "2.1": 1,
    "2.2": 2,
}


def parse_version(version_str):
    """Returns a version string like "2.2.0" as a tuple of ints, or None if it isn't one."""
    match = re.match(r"^(\d+)\.(\d+)\.(\d+)$", version_str.strip())
    return tuple(int(x) for x in match.groups()) if match else None


def firmware_version_from_path(path):
    """Returns the version of a Master_hub_X.Y.Z.cyacd file from its name, or None."""
    match = re.match(r"^Master_hub_(.+)\.cyacd$", os.path.basename(path))
    return parse_version(match.group(1)) if match else None


def find_latest_firmware(family):
    """Find the latest Master_hub firmware file of a family (major version) in the bin/ directory."""
    files = {}
    for path in glob.glob(os.path.join(BIN_DIR, "Master_hub_*.cyacd")):
        version = firmware_version_from_path(path)
        if version and version[0] == family:
            files[version] = path
    return files[max(files)] if files else None


def format_version(version):
    return ".".join(str(x) for x in version) if version else "unknown"


JTAG_ID = 0x2E15A069  # CY8C5288LTI-LP090
DEVICE_REV = 0
BOOTLOADER_REV = 0


class Bootloader():
    MAX_DATA_LENGTH = 64-7-9

    STATUSES = {
        0x00: "CYRET_SUCCESS",
        0x03: "BOOTLOADER_ERR_LENGTH",
        0x04: "BOOTLOADER_ERR_DATA",
        0x05: "BOOTLOADER_ERR_CMD",
        0x08: "BOOTLOADER_ERR_CHECKSUM",
        0x09: "BOOTLOADER_ERR_ARRAY",
        0x0a: "BOOTLOADER_ERR_ROW",
        0x0c: "BOOTLOADER_ERR_APP",
        0x0d: "BOOTLOADER_ERR_ACTIVE",
        0x0e: "BOOTLOADER_ERR_CALLBACK",
        0x0f: "BOOTLOADER_ERR_UNK"
    }

    def __init__(self, vid=0, pid=0, serial=None):
        self._device = hid.device()
        try:
            self._device.open(vid, pid)
        except OSError as error:
            sys.stderr.write("Error: {}\n".format(error))
            sys.stderr.write("Make sure you have permissions to access the HID device (try running with sudo or adding a udev rule).\n")
            raise

        self.jtag_id = JTAG_ID
        self.device_revision = DEVICE_REV
        self.bootloader_revision = BOOTLOADER_REV

    def _checksum(self, data):
        '''16-bit packet checksum.

        :param data: The data to sum
        :type data: bytes or list of 8-bit integers
        :returns: checksum
        :rtype: int
        '''
        checksum = 0
        for byte in data:
            checksum += byte
        checksum = (~checksum + 1) & 0xffff
        return checksum

    def _make_packet(self, command, data=[]):
        '''Generates a packet to send the bootloader.

        :param command: The command to send
        :type command: 8-bit int
        :param data: The data to send
        :type data: bytes or list of 8-bit integers
        :returns: Packet data
        :rtype: list of 8-bit integers
        '''
        packet = []
        packet.append(0x00) # Byte supplémentaire à envoyé pour Report ID du protocol USB. (Ce Byte n'est pas en lien avec le bootloader)
        packet.append(0x01)

        packet.append(command)

        data_length = len(data)
        assert data_length <= (64 - 7)
        packet.append(data_length & 0xff)
        packet.append((data_length >> 8) & 0xff)

        for byte in data:
            packet.append(byte & 0xff)

        checksum = self._checksum(packet)
        packet.append(checksum & 0xff)
        packet.append((checksum >> 8) & 0xff)

        packet.append(0x17)
        return packet

    def _parse_response(self, response_data):
        '''Parses the bootloader's response.

        :param response_data: The response_data to parse
        :type response_data: list of 8-bit integers
        :returns: Response data
        :rtype: dict
        '''
        response = {
            "status": "BOOTLOADER_ERR_UNK",
            "data": [],
            "checksum_ok": False,
        }

        sop = response_data[0]
        assert sop == 0x01

        response["status"] = self.STATUSES[response_data[1]]

        data_length = (response_data[3] << 8) | response_data[2]

        if data_length > 0:
            response["data"] = response_data[4:4+data_length]

        checksum_received = response_data[4+data_length]
        checksum_received |= response_data[4+data_length+1] << 8
        checksum_calculated = self._checksum(response_data[:4+data_length])
        response["checksum_ok"] = (checksum_calculated == checksum_received)

        eop = response_data[4+data_length+2]
        assert eop == 0x17

        return response

    def send_command(self, command, data=[]):
        '''Sends a command with data to the bootloader.

        :param command: The command to send
        :type command: 8-bit int
        :param data: The data to send
        :type data: bytes or list of 8-bit integers
        :returns: Response data
        :rtype: dict
        '''
        packet = self._make_packet(command, data)
        self._device.write(bytes(packet))

        response_data = self._device.read(64)
        response = self._parse_response(response_data)
        return response

    def enter_bootloader(self):
        '''Sends the "Enter Bootloader" command.

        :returns: Success/failure
        :rtype: bool
        '''
        #return True
        response = self.send_command(0x38)
        if response["checksum_ok"] and response["status"] == "CYRET_SUCCESS":
            self.jtag_id = response["data"][0]
            self.jtag_id |= response["data"][1] << 8
            self.jtag_id |= response["data"][2] << 16
            self.jtag_id |= response["data"][3] << 24
            self.device_revision = response["data"][4]
            self.bootloader_revision = response["data"][5:8][::-1]
            return True
        else:
            return False

    def program_row(self, array_id, row_number, data):
        '''Programs a row of flash.

        :param array_id: The array_id of the row to program
        :type array_id: 8-bit int
        :param row_number: The row_number of the row to program
        :type row_number: 16-bit int
        :param data: The data to program
        :type data: bytes or list of 8-bit integers
        :returns: Success/failure
        :rtype: bool
        '''
        arguments = [array_id, (row_number & 0xff), (row_number >> 8)]
        response = self.send_command(0x39, arguments + data)
        if response["checksum_ok"] and response["status"] == "CYRET_SUCCESS":
            return True
        else:
            return False

    def erase_row(self, array_id, row_number):
        '''Erases a row of flash.

        :param array_id: The array_id of the row to erase
        :type array_id: 8-bit int
        :param row_number: The row_number of the row to erase
        :type row_number: 16-bit int
        :returns: Success/failure
        :rtype: bool
        '''
        arguments = [array_id, (row_number & 0xff), (row_number >> 8)]
        response = self.send_command(0x34, arguments)
        if response["checksum_ok"] and response["status"] == "CYRET_SUCCESS":
            return True
        else:
            return False

    def send_data(self, data):
        '''Sends data to the bootloader to be used by another command.

        :param data: The data to send
        :type data: bytes or list of 8-bit integers
        :returns: Success/failure
        :rtype: bool
        '''
        response = self.send_command(0x37, data)
        if response["checksum_ok"] and response["status"] == "CYRET_SUCCESS":
            return True
        else:
            return False

    def verify_checksum(self):
        '''Sends the "Verify Checksum" command.

        :returns: True if the application checksum is valid
        :rtype: bool
        '''
        response = self.send_command(0x31)
        if response["checksum_ok"] and response["status"] == "CYRET_SUCCESS" and len(response["data"]) > 0:
            return response["data"][0] != 0
        else:
            return False

    def exit_bootloader(self):
        '''Sends the "Exit Bootloader" command.'''
        packet = self._make_packet(0x3b, [])
        self._device.write(bytes(packet))

    def flash(self, firmware):
        ret = False
        firmware_data = firmware.firmware
        for row in firmware_data:
            array_id = row[0]
            row_number = row[1]
            data = list(row[2])
            if len(data) >= self.MAX_DATA_LENGTH:
                full_packets = len(data)//self.MAX_DATA_LENGTH
                partial_packet_length = len(data)%self.MAX_DATA_LENGTH
                if partial_packet_length > 0:
                    for i in range(0, full_packets):
                        ret = self.send_data(data[self.MAX_DATA_LENGTH*i:self.MAX_DATA_LENGTH*i+self.MAX_DATA_LENGTH])
                        if not ret:
                            return False
                    ret = self.program_row(array_id, row_number, data[self.MAX_DATA_LENGTH*full_packets:self.MAX_DATA_LENGTH*full_packets+partial_packet_length])
                    if not ret:
                        return False
                else:
                    for i in range(0, full_packets-1):
                        ret = self.send_data(data[self.MAX_DATA_LENGTH*i:self.MAX_DATA_LENGTH*i+self.MAX_DATA_LENGTH])
                        if not ret:
                            return False
                    ret = self.program_row(array_id, row_number, data[self.MAX_DATA_LENGTH*(full_packets-1):self.MAX_DATA_LENGTH*(full_packets-1)+self.MAX_DATA_LENGTH])
                    if not ret:
                        return False
            else:
                ret = self.program_row(array_id, row_number, data)
                if not ret:
                    return False
        
        return ret

class Cyacd():
    def __init__(self, firmware_file):
        self.file = firmware_file
        self.silicon_id = None
        self.silicon_revision = None
        self.checksum_type = None
        self.firmware = None

    def _checksum(self, data):
        '''Single-byte flash row checksum.

        :param data: The data to sum
        :type data: bytes or list of 8-bit integers
        :returns: checksum
        :rtype: int
        '''
        checksum = 0
        for byte in data:
            checksum += byte
        checksum = (~checksum + 1) & 0xff
        return checksum

    def parse(self):
        '''Read's the firmware into a list of (array_id, row_number, data)
        tuples.
        '''
        lines = self.file.readlines()

        header_line = lines.pop(0).rstrip('\r\n')
        header = binascii.a2b_hex(header_line)
        self.silicon_id = header[3]
        self.silicon_id |= header[2] << 8
        self.silicon_id |= header[1] << 16
        self.silicon_id |= header[0] << 24
        self.silicon_revision = header[4]
        self.checksum_type = header[5]

        firmware = []
        for line in lines:
            line = binascii.a2b_hex(line[1:].rstrip('\r\n'))
            array_id = line[0]
            row_number = (line[1] << 8) | line[2]
            data_length = (line[3] << 8) | line[4]
            data = line[5:5+data_length]
            checksum = line[5+data_length]
            checksum_calculated = self._checksum(line[:-1])
            assert checksum == checksum_calculated
            firmware.append((array_id, row_number, data))

        self.firmware = firmware


def open_serial_by_vid_pid(vid, pid, baudrate=115200):
    # 1. Lister tous les ports COM disponibles
    ports = serial.tools.list_ports.comports()
    target_port = None

    for port in ports:
        # Vérification de la correspondance VID/PID
        if port.vid == vid and port.pid == pid:
            target_port = port.device
            break

    if target_port:
        try:
            ser = serial.Serial(
                port=target_port,
                baudrate=baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1
            )
            return ser
        except Exception as e:
            sys.stderr.write("Error while opening port {}: {}\n".format(target_port, e))
            return None
    else:
        return None


def make_usb_packet(command, data=b""):
    """Builds a Master Hub application packet: start byte, CRC (unused, always 0), command, length, data."""
    return bytes([USB_PACKET_START_BYTE, 0x00, command, len(data)]) + bytes(data)


def request_usb_reply(serial_device, command, timeout=1.0):
    """Sends a command to the Master Hub application and returns the data of its reply, or None on timeout."""
    serial_device.reset_input_buffer()
    serial_device.write(make_usb_packet(command))

    buffer = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        buffer += serial_device.read(serial_device.in_waiting or 1)

        # Parse every complete packet in the buffer, dropping bytes until a start byte
        while True:
            start = buffer.find(USB_PACKET_START_BYTE)
            if start < 0:
                buffer.clear()
                break
            del buffer[:start]
            if len(buffer) < 4 or len(buffer) < 4 + buffer[3]:
                break
            packet_command, data = buffer[2], bytes(buffer[4:4 + buffer[3]])
            del buffer[:4 + buffer[3]]
            if packet_command == command:
                return data

    return None


def read_hub_device_info(serial_device):
    """Reads the hub device info stored in EEPROM at production.

    :returns: ("valid", (hardware_version, serial_number)), ("blank", None) when missing, unreadable or never
              programmed, or ("inconsistent", (hardware_version, serial_number)) when valid but not a hub
    """
    data = request_usb_reply(serial_device, USB_COMMAND_GET_DEVICES_INFO)
    if data is None or len(data) != DEVICE_INFO_ROW_SIZE * DEVICE_INFO_ROW_COUNT:
        return "blank", None

    row = data[:DEVICE_INFO_ROW_SIZE]
    if (sum(row[:-1]) & 0xFF) != row[-1] or row[0] != DEVICE_INFO_ID:
        return "blank", None

    hardware_version, serial_number = row[1:5], row[5:13]
    if hardware_version[0] != DEVICE_INFO_HUB_TYPE or not serial_number.startswith(DEVICE_INFO_HUB_SERIAL_PREFIX):
        return "inconsistent", (hardware_version, serial_number)
    return "valid", (hardware_version, serial_number)


def detect_hardware_revision(serial_device, old_vid_pid):
    """Detects the hardware revision of a Master Hub running its application.

    :returns: (hardware revision or None if it can't be told, firmware version running or None)
    """
    # Stop any sensor stream left running so it doesn't hide the replies
    serial_device.write(make_usb_packet(USB_COMMAND_AUTOSEND_ASYNC_SENSORS, b"\x00"))
    time.sleep(0.1)

    reply = request_usb_reply(serial_device, USB_COMMAND_GET_FIRMWARE_VERSION)
    running_version = parse_version(reply.split(b"\x00")[0].decode("ascii", "replace")) if reply else None
    sys.stderr.write("Firmware running: {}\n".format(format_version(running_version)))

    # The old VID/PID predates hardware revision 2.2
    if old_vid_pid:
        return "2.1", running_version

    if running_version is None:
        sys.stderr.write("Error: The device did not report its firmware version.\n")
        return None, None

    if running_version[0] == FIRMWARE_FAMILY["2.1"]:
        return "2.1", running_version

    if running_version[0] == FIRMWARE_FAMILY["2.2"]:
        status, info = read_hub_device_info(serial_device)
        if status == "valid":
            sys.stderr.write("Hub serial number: {}\n".format(info[1].decode("ascii", "replace")))
        elif status == "blank":
            sys.stderr.write("Warning: No device info in the hub EEPROM. "
                             "Assuming hardware revision 2.2 from the firmware version.\n")
        else:
            sys.stderr.write("Error: The hub EEPROM holds unexpected device info (hardware version {}, "
                             "serial number {}).\n".format(info[0].hex(), info[1].decode("ascii", "replace")))
            return None, running_version
        return "2.2", running_version

    sys.stderr.write("Error: Unknown firmware family {}.\n".format(format_version(running_version)))
    return None, running_version


def confirm_force(reasons):
    """Asks the user to confirm flashing despite failed compatibility checks."""
    sys.stderr.write("\n"
                     "WARNING: --force skips the hardware compatibility check:\n")
    for reason in reasons:
        sys.stderr.write("  - {}\n".format(reason))
    sys.stderr.write("Flashing firmware made for another hardware revision can leave the hub and its fingers\n"
                     "unusable. The hub also reprograms the fingers with the firmware it carries.\n"
                     "Only continue if Robotiq support told you to.\n")
    try:
        answer = input("Type FORCE to continue: ")
    except EOFError:
        answer = ""
    return answer.strip() == "FORCE"


def main():
    parser = argparse.ArgumentParser(
        description="Flash firmware to the Master Hub via USB bootloader. By default, flashes the latest "
                    "firmware in bin/ for the hardware revision of the connected hub.")
    parser.add_argument("--firmware", type=str, default=None,
                        help="Path to a specific Master_hub_X.Y.Z.cyacd firmware file")
    parser.add_argument("--latest", action="store_true",
                        help="Kept for compatibility: flashing the latest firmware is now the default")
    parser.add_argument("--hw-rev", choices=sorted(FIRMWARE_FAMILY), default=None,
                        help="Hardware revision, required when the hub is already in its bootloader "
                             "(finger serial numbers: 21xxx = 2.1, 22xxx = 2.2)")
    parser.add_argument("--force", action="store_true",
                        help="Skip the hardware compatibility check (dangerous, asks for confirmation)")
    args = parser.parse_args()

    vid = BOOTLOADER_VID
    pid = BOOTLOADER_PID

    ret = False

    # Search for a device in application mode
    old_vid_pid = False
    serial_device = open_serial_by_vid_pid(MASTER_HUB_APP_VID, MASTER_HUB_APP_PID)
    if not serial_device:
        serial_device = open_serial_by_vid_pid(MASTER_HUB_APP_VID_OLD, MASTER_HUB_APP_PID_OLD)
        old_vid_pid = serial_device is not None

    # Detect the hardware revision while the application is still running
    hw_rev = None
    if serial_device and serial_device.is_open:
        sys.stderr.write("Device found on {}.\n".format(serial_device.port))
        serial_device.timeout = 0.1
        hw_rev, _ = detect_hardware_revision(serial_device, old_vid_pid)
    else:
        sys.stderr.write("No device in application mode found. The hardware revision can't be detected.\n")

    force_reasons = []
    if hw_rev and args.hw_rev and hw_rev != args.hw_rev:
        force_reasons.append("--hw-rev {} doesn't match the detected hardware revision {}".format(args.hw_rev, hw_rev))
    hw_rev = hw_rev or args.hw_rev
    if hw_rev:
        sys.stderr.write("Hardware revision: {}\n".format(hw_rev))

    # Pick the firmware and check it matches the hardware revision
    if args.firmware:
        firmware_file = args.firmware
        firmware_version = firmware_version_from_path(firmware_file)
        if hw_rev is None:
            force_reasons.append("the hardware revision is unknown (use --hw-rev)")
        elif firmware_version is None:
            force_reasons.append("the firmware version can't be read from the file name (expected Master_hub_X.Y.Z.cyacd)")
        elif firmware_version[0] != FIRMWARE_FAMILY[hw_rev]:
            force_reasons.append("firmware {} is not made for hardware revision {} (needs {}.x)".format(
                format_version(firmware_version), hw_rev, FIRMWARE_FAMILY[hw_rev]))
    else:
        if hw_rev is None:
            sys.stderr.write("Error: Unknown hardware revision. Pass --hw-rev 2.1 or 2.2 "
                             "(finger serial numbers: 21xxx = 2.1, 22xxx = 2.2).\n")
            sys.exit(1)
        firmware_file = find_latest_firmware(FIRMWARE_FAMILY[hw_rev])
        if not firmware_file:
            sys.stderr.write("Error: No {}.x firmware files found in {}\n".format(
                FIRMWARE_FAMILY[hw_rev], os.path.abspath(BIN_DIR)))
            sys.exit(1)

    sys.stderr.write("Firmware: {}\n".format(os.path.basename(firmware_file)))

    if force_reasons:
        if not args.force:
            for reason in force_reasons:
                sys.stderr.write("Error: {}.\n".format(reason[0].upper() + reason[1:]))
            sys.stderr.write("Nothing was flashed.\n")
            sys.exit(1)
        if not confirm_force(force_reasons):
            sys.stderr.write("Aborted. Nothing was flashed.\n")
            sys.exit(1)

    filename = firmware_file

    # Reboot the device into bootloader
    if serial_device and serial_device.is_open:
        sys.stderr.write("Rebooting into bootloader...\n")
        serial_device.write(make_usb_packet(USB_COMMAND_ENTER_BOOTLOADER))
        serial_device.close()
        time.sleep(1)
    else:
        sys.stderr.write("Looking for bootloader directly...\n")

    # Wait for the bootloader HID device to appear (may take a few seconds after reboot)
    found = False
    for attempt in range(5):
        for enumerated in hid.enumerate():
            if enumerated["vendor_id"] == vid and enumerated["product_id"] == pid:
                found = True
                break
        if found:
            break
        sys.stderr.write("Waiting for bootloader device to appear (attempt {}/5)...\n".format(attempt + 1))
        time.sleep(1)

    if found:
        # Connect to the device's bootloader
        try:
            bootloader = Bootloader(vid, pid)
        except OSError:
            sys.exit(1)
        ret = bootloader.enter_bootloader()
        if not ret:
            sys.stderr.write("Unable to communicate with bootloader!\n")
            sys.exit(1)

        # Load the firmware file
        firmware = Cyacd(open(filename, 'r'))
        firmware.parse()

        # Make sure the firmware is being flashed to the correct chip
        if (bootloader.jtag_id != firmware.silicon_id) or (bootloader.device_revision != firmware.silicon_revision):
            sys.stderr.write("Error: Firmware is for silicon 0x{:08X} rev {}, device is 0x{:08X} rev {}. Nothing was flashed.\n".format(
                firmware.silicon_id, firmware.silicon_revision, bootloader.jtag_id, bootloader.device_revision))
            bootloader.exit_bootloader()
            sys.exit(1)

        sys.stderr.write("Programming firmware application...\n")
        ret = bootloader.flash(firmware)
        if not ret:
            sys.stderr.write("Error while programming! The device stays in bootloader: run again with --hw-rev.\n")
            sys.exit(1)

        if not bootloader.verify_checksum():
            sys.stderr.write("Error: Application checksum is invalid! The device stays in bootloader: run again with --hw-rev.\n")
            sys.exit(1)

        bootloader.exit_bootloader()

        sys.stderr.write("Device programmed successfully!\n")

    else:
        sys.stderr.write("Bootloader not found!\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
