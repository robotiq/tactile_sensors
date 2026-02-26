"""
USB Protocol Implementation for Robotiq Tactile Sensor
Based on the protocol from tactile_sensor_ui/src/communicator.cpp
"""

import struct
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

# Protocol Constants
USB_PACKET_START_BYTE = 0x9A
USB_PACKET_MAX_SIZE = 64
USB_PACKET_HEADER_SIZE = 4
USB_PACKET_MAX_DATA_SIZE = 60

# Commands
USB_COMMAND_READ_SENSORS = 0x61
USB_COMMAND_AUTOSEND_SENSORS = 0x58
USB_COMMAND_ENTER_BOOTLOADER = 0xE2
USB_COMMAND_GET_VERSION = 0XE3

# Sensor Type Identifiers (high nibble)
SENSOR_TYPE_STATIC_TACTILE = 0x10
SENSOR_TYPE_DYNAMIC_TACTILE = 0x20
SENSOR_TYPE_ACCELEROMETER = 0x30
SENSOR_TYPE_GYROSCOPE = 0x40
SENSOR_TYPE_MAGNETOMETER = 0x50
SENSOR_TYPE_TEMPERATURE = 0x60
SENSOR_TYPE_TIMESTAMP = 0x70

# Sensor sizes
STATIC_TACTILE_SIZE = 28  # 7x4 grid (7 rows, 4 columns)
DYNAMIC_TACTILE_SIZE = 1
IMU_SIZE = 3  # 3 axes
NUM_FINGERS = 2


@dataclass
class FingerData:
    """Data structure for a single finger"""
    static_tactile: List[int] = None  # 28 uint16 values
    dynamic_tactile: int = 0  # 1 int16 value
    accelerometer: List[int] = None  # 3 int16 values [x, y, z]
    gyroscope: List[int] = None  # 3 int16 values [x, y, z]
    magnetometer: List[int] = None  # 3 int16 values [x, y, z]
    temperature: int = 0  # 1 int16 value
    timestamp: int = 0  # 1 uint64 value

    def __post_init__(self):
        if self.static_tactile is None:
            self.static_tactile = [0] * STATIC_TACTILE_SIZE
        if self.accelerometer is None:
            self.accelerometer = [0, 0, 0]
        if self.gyroscope is None:
            self.gyroscope = [0, 0, 0]
        if self.magnetometer is None:
            self.magnetometer = [0, 0, 0]


@dataclass
class SensorData:
    """Complete sensor data for all fingers"""
    fingers: List[FingerData] = None

    def __post_init__(self):
        if self.fingers is None:
            self.fingers = [FingerData() for _ in range(NUM_FINGERS)]


class UsbPacketParser:
    """Parser for USB packets from tactile sensor"""

    def __init__(self):
        self.buffer = bytearray()
        self.sensor_data = SensorData()
        self.packet_count = 0

    def create_command_packet(self, command: int, data: bytes = b'') -> bytes:
        """Create a command packet to send to the sensor"""
        data_length = len(data)
        if data_length > USB_PACKET_MAX_DATA_SIZE:
            raise ValueError(f"Data too large: {data_length} > {USB_PACKET_MAX_DATA_SIZE}")

        # Build packet: start_byte, crc8 (placeholder), command, data_length, data
        packet = bytearray([USB_PACKET_START_BYTE])
        packet.append(0x00)  # CRC8 placeholder (not implemented in original code)
        packet.append(command)
        packet.append(data_length)
        packet.extend(data)

        return bytes(packet)

    def create_get_version_command(self) -> bytes:
        """Create command to request firmware version"""
        return self.create_command_packet(USB_COMMAND_GET_VERSION)

    def print_firmware_version(self, serial_port, timeout: float = 2.0):
        """Request and print the firmware version from the sensor."""
        serial_port.write(self.create_get_version_command())
        serial_port.flush()

        deadline = time.time() + timeout
        while time.time() < deadline:
            data = serial_port.read(64)
            if not data:
                continue
            for packet in self.feed_bytes(data):
                if len(packet) >= USB_PACKET_HEADER_SIZE and packet[2] == USB_COMMAND_GET_VERSION:
                    print(f"Firmware version: {packet[USB_PACKET_HEADER_SIZE:].decode('ascii', errors='replace')}")
                    return
        print("Warning: Could not read firmware version")

    def create_autosend_command(self, period_ms: int = 1) -> bytes:
        """Create autosend command with specified period in milliseconds"""
        # Period is sent as 1 byte (max value 255)
        if period_ms > 255:
            raise ValueError(f"Period must be <= 255ms, got {period_ms}")
        period_byte = bytes([period_ms])
        return self.create_command_packet(USB_COMMAND_AUTOSEND_SENSORS, period_byte)

    def feed_bytes(self, data: bytes) -> List[bytes]:
        """
        Feed incoming bytes to the parser.
        Returns list of complete packets found.
        """
        self.buffer.extend(data)
        packets = []

        while len(self.buffer) >= USB_PACKET_HEADER_SIZE:
            # Search for start byte
            start_idx = self.buffer.find(USB_PACKET_START_BYTE)

            if start_idx == -1:
                # No start byte found, clear buffer
                self.buffer.clear()
                break

            # Remove bytes before start byte
            if start_idx > 0:
                self.buffer = self.buffer[start_idx:]

            # Check if we have enough bytes for header
            if len(self.buffer) < USB_PACKET_HEADER_SIZE:
                break

            # Parse header
            # start_byte = self.buffer[0]  # Already verified as 0x9A
            # crc8 = self.buffer[1]  # Not validated in original code
            # command = self.buffer[2]
            data_length = self.buffer[3]

            # Check if we have complete packet
            packet_size = USB_PACKET_HEADER_SIZE + data_length
            if len(self.buffer) < packet_size:
                # Wait for more data
                break

            # Extract complete packet
            packet = bytes(self.buffer[:packet_size])
            self.buffer = self.buffer[packet_size:]
            packets.append(packet)
            self.packet_count += 1

        return packets

    def parse_sensor_packet(self, packet: bytes) -> bool:
        """
        Parse a sensor data packet and update sensor_data.
        Returns True if dynamic tactile data was received (indicates complete data set).
        Based on parseSensors() from communicator.cpp
        """
        if len(packet) < USB_PACKET_HEADER_SIZE:
            return False

        data = packet[USB_PACKET_HEADER_SIZE:]
        data_length = len(data)

        idx = 0
        received_dynamic = False

        while idx < data_length:
            if idx >= len(data):
                break

            # First byte encodes sensor type and finger ID
            sensor_byte = data[idx]
            idx += 1

            # Extract sensor type (high nibble)
            sensor_type = sensor_byte & 0xF0

            # Extract finger ID (bits 2-3)
            finger_id = (sensor_byte >> 2) & 0x03

            if finger_id >= len(self.sensor_data.fingers):
                continue

            finger = self.sensor_data.fingers[finger_id]

            # Parse sensor data based on type
            if sensor_type == SENSOR_TYPE_STATIC_TACTILE:
                # 28 uint16 values (56 bytes)
                values, bytes_consumed = self._extract_uint16_array(data[idx:], STATIC_TACTILE_SIZE)
                finger.static_tactile = values
                idx += bytes_consumed

            elif sensor_type == SENSOR_TYPE_DYNAMIC_TACTILE:
                # 1 int16 value (2 bytes)
                values, bytes_consumed = self._extract_int16_array(data[idx:], DYNAMIC_TACTILE_SIZE)
                if len(values) > 0:
                    finger.dynamic_tactile = values[0]
                    received_dynamic = True
                idx += bytes_consumed

            elif sensor_type == SENSOR_TYPE_ACCELEROMETER:
                # 3 int16 values (6 bytes)
                values, bytes_consumed = self._extract_int16_array(data[idx:], IMU_SIZE)
                finger.accelerometer = values if len(values) == IMU_SIZE else [0, 0, 0]
                idx += bytes_consumed

            elif sensor_type == SENSOR_TYPE_GYROSCOPE:
                # 3 int16 values (6 bytes)
                values, bytes_consumed = self._extract_int16_array(data[idx:], IMU_SIZE)
                finger.gyroscope = values if len(values) == IMU_SIZE else [0, 0, 0]
                idx += bytes_consumed

            elif sensor_type == SENSOR_TYPE_MAGNETOMETER:
                # 3 int16 values (6 bytes)
                values, bytes_consumed = self._extract_int16_array(data[idx:], IMU_SIZE)
                finger.magnetometer = values if len(values) == IMU_SIZE else [0, 0, 0]
                idx += bytes_consumed

            elif sensor_type == SENSOR_TYPE_TEMPERATURE:
                # 1 int16 value (2 bytes)
                values, bytes_consumed = self._extract_int16_array(data[idx:], 1)
                if len(values) > 0:
                    finger.temperature = values[0]
                idx += bytes_consumed

            elif sensor_type == SENSOR_TYPE_TIMESTAMP:
                # 1 uint64 value (8 bytes)
                values, bytes_consumed = self._extract_uint64_array(data[idx:], 1)
                if len(values) > 0:
                    finger.timestamp = values[0]
                idx += bytes_consumed

        return received_dynamic

    def _extract_uint16_array(self, data: bytes, count: int) -> Tuple[List[int], int]:
        """Extract array of big-endian uint16 values"""
        values = []
        bytes_needed = count * 2
        bytes_available = min(len(data), bytes_needed)

        for i in range(0, bytes_available, 2):
            if i + 1 < len(data):
                # Big-endian: MSB first
                value = (data[i] << 8) | data[i + 1]
                values.append(value)

        return values, bytes_available

    def _extract_uint64_array(self, data: bytes, count: int) -> Tuple[List[int], int]:
        """Extract array of big-endian uint64 values"""
        values = []
        bytes_needed = count * 8
        bytes_available = min(len(data) // 8 * 8, bytes_needed)

        for i in range(0, bytes_available, 8):
            value = (
                (data[i]     << 56) |
                (data[i + 1] << 48) |
                (data[i + 2] << 40) |
                (data[i + 3] << 32) |
                (data[i + 4] << 24) |
                (data[i + 5] << 16) |
                (data[i + 6] << 8)  |
                (data[i + 7])
            )
            values.append(value)

        return values, bytes_available

    def _extract_int16_array(self, data: bytes, count: int) -> Tuple[List[int], int]:
        """Extract array of big-endian int16 values"""
        values = []
        bytes_needed = count * 2
        bytes_available = min(len(data), bytes_needed)

        for i in range(0, bytes_available, 2):
            if i + 1 < len(data):
                # Big-endian: MSB first
                value = (data[i] << 8) | data[i + 1]
                # Convert to signed int16
                if value >= 32768:
                    value -= 65536
                values.append(value)

        return values, bytes_available

    def get_sensor_data(self) -> SensorData:
        """Get the current sensor data"""
        return self.sensor_data
