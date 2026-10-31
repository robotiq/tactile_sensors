#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2016  Forest Crossman <cyrozap@gmail.com>
# Copyright (C) 2026  Robotiq
#
# Based on cyrozap/Cypress-HID-Bootloader-Host; modified 2026 by Robotiq for
# Master Hub integration (device VID/PIDs, firmware selection, and the serial
# reboot-to-bootloader handshake).
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

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BIN_DIR = os.path.join(SCRIPT_DIR,  "bin")
BOOTLOADER_VID = 0x04B4
BOOTLOADER_PID = 0xB71D

MASTER_HUB_APP_VID_OLD = 0x04B4
MASTER_HUB_APP_PID_OLD = 0xF232

MASTER_HUB_APP_VID = 0x16D0
MASTER_HUB_APP_PID = 0x14CC


DEFAULT_FIRMWARE_VERSION = "1.1.8" # latest firmware version out in the field
DEFAULT_FIRMWARE_FILE = os.path.join(BIN_DIR, "Master_hub_{}.cyacd".format(DEFAULT_FIRMWARE_VERSION))


def find_latest_firmware():
    """Find the latest Master_hub firmware file in the bin/ directory by version number."""
    pattern = os.path.join(BIN_DIR, "Master_hub_*.cyacd")
    files = glob.glob(pattern)
    if not files:
        return None
    def version_key(path):
        name = os.path.basename(path)
        version_str = name.replace("Master_hub_", "").replace(".cyacd", "")
        return tuple(int(x) for x in version_str.split("."))
    return max(files, key=version_key)


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


def main():
    parser = argparse.ArgumentParser(description="Flash firmware to the Master Hub via USB bootloader.")
    parser.add_argument("--firmware", type=str, default=None,
                        help="Path to a specific .cyacd firmware file")
    parser.add_argument("--latest", action="store_true",
                        help="Flash the latest firmware found in bin/ instead of the default v{}".format(DEFAULT_FIRMWARE_VERSION))
    args = parser.parse_args()

    if args.latest:
        firmware_file = find_latest_firmware()
        if not firmware_file:
            sys.stderr.write("Error: No firmware files found in {}\n".format(os.path.abspath(BIN_DIR)))
            sys.exit(1)
    elif args.firmware:
        firmware_file = args.firmware
    else:
        firmware_file = DEFAULT_FIRMWARE_FILE

    sys.stderr.write("Firmware: {}\n".format(os.path.basename(firmware_file)))

    vid = BOOTLOADER_VID
    pid = BOOTLOADER_PID
    filename = firmware_file

    ret = False

    # Search for a device in application mode and reboot it into bootloader
    serial_device = open_serial_by_vid_pid(MASTER_HUB_APP_VID, MASTER_HUB_APP_PID)
    if not serial_device:
       serial_device = open_serial_by_vid_pid(MASTER_HUB_APP_VID_OLD, MASTER_HUB_APP_PID_OLD)

    if serial_device and serial_device.is_open:
        sys.stderr.write("Device found on {}. Rebooting into bootloader...\n".format(serial_device.port))
        packet = [0x9A, 0x00, 0xE2, 0x00]
        serial_device.write(bytes(packet))
        serial_device.close()
        time.sleep(1)
    else:
        sys.stderr.write("No device in application mode found. Looking for bootloader directly...\n")

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
            sys.stderr.write("Enable to communicate with bootloader!\n")
            sys.exit(1)

        # Load the firmware file
        firmware = Cyacd(open(filename, 'r'))
        firmware.parse()

        # Make sure the firmware is being flashed to the correct chip
        if (bootloader.jtag_id == firmware.silicon_id) and (bootloader.device_revision == firmware.silicon_revision):
            sys.stderr.write("Programming firmware application...\n")
            ret = bootloader.flash(firmware)
            if not ret:
                sys.stderr.write("Error while programming!\n")
                return

        ret = bootloader.exit_bootloader()

        sys.stderr.write("Device programmed successfully!\n")

    else:
        sys.stderr.write("Bootloader not found!\n")
        

if __name__ == "__main__":
    main()