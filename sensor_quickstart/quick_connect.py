#!/usr/bin/env python3
"""
Simple Tactile Sensor Check Tool
Detects, connects to, and displays data from Robotiq Tactile Sensors
"""

import sys
import argparse
import select
import time
import signal
import shutil
from typing import Optional, List, Dict, Tuple
import math
import os
from collections import defaultdict


try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: pyserial not installed")
    print("Please install with: pip install pyserial")
    sys.exit(1)

from protocol import UsbPacketParser, SensorData, FingerData
from protocol import (
    UsbPacketParser, SensorData,
    SENSOR_TYPE_STATIC_TACTILE, SENSOR_TYPE_DYNAMIC_TACTILE,
    SENSOR_TYPE_ACCELEROMETER, SENSOR_TYPE_GYROSCOPE,
    SENSOR_TYPE_MAGNETOMETER, SENSOR_TYPE_TEMPERATURE,
    SENSOR_TYPE_TIMESTAMP,
    STATIC_TACTILE_SIZE, DYNAMIC_TACTILE_SIZE, IMU_SIZE,
    USB_PACKET_HEADER_SIZE, USB_COMMAND_GET_VERSION,
)

# Tactile sensor Hub VID/PID
MASTER_HUB_APP_VID_OLD = 0x04B4
MASTER_HUB_APP_PID_OLD = 0xF232
MASTER_HUB_APP_VID = 0x16D0
MASTER_HUB_APP_PID = 0x14CC

# Serial port configuration
BAUD_RATE = 115200
DATA_BITS = 8
PARITY = 'N'
STOP_BITS = 1
TIMEOUT = 0.1  # 100ms timeout for reads

# Display configuration
NUM_FINGERS = 2  # Currently 2 fingers
REFRESH_RATE_WINDOW = 1.0  # Calculate refresh rate over 1 second

# ── Display helpers for Field Tracker ────────────────────────────────────────────────────────────
FIELD_LABEL = {
    SENSOR_TYPE_STATIC_TACTILE:  "Static Tactile",
    SENSOR_TYPE_DYNAMIC_TACTILE: "Dynamic Tactile",
    SENSOR_TYPE_ACCELEROMETER:   "Accelerometer",
    SENSOR_TYPE_GYROSCOPE:       "Gyroscope",
    SENSOR_TYPE_TEMPERATURE:     "Temperature",
    SENSOR_TYPE_TIMESTAMP:       "Timestamp",
}
FIELD_ORDER = [
    SENSOR_TYPE_STATIC_TACTILE,
    SENSOR_TYPE_DYNAMIC_TACTILE,
    SENSOR_TYPE_ACCELEROMETER,
    SENSOR_TYPE_GYROSCOPE,
    SENSOR_TYPE_TEMPERATURE,
    SENSOR_TYPE_TIMESTAMP,
]

# Field payload sizes in bytes (uint16 = 2 bytes each)
_FIELD_BYTE_SIZES = {
    SENSOR_TYPE_STATIC_TACTILE:  STATIC_TACTILE_SIZE * 2,   # 56
    SENSOR_TYPE_DYNAMIC_TACTILE: DYNAMIC_TACTILE_SIZE * 2,  # 2
    SENSOR_TYPE_ACCELEROMETER:   IMU_SIZE * 2,               # 6
    SENSOR_TYPE_GYROSCOPE:       IMU_SIZE * 2,               # 6
    SENSOR_TYPE_TEMPERATURE:     2,
    SENSOR_TYPE_TIMESTAMP:       8,  # uint64 in new firmware protocol
}


def _delta_stats(deltas: List[int]) -> Optional[Dict]:
    """Return mean/std/min/max/count for a list of timestamp deltas, or None."""
    if not deltas:
        return None
    n = len(deltas)
    mean = sum(deltas) / n
    variance = sum((d - mean) ** 2 for d in deltas) / n if n > 1 else 0.0
    return {
        "mean":  mean,
        "std":   math.sqrt(variance),
        "min":   min(deltas),
        "max":   max(deltas),
        "count": n,
    }


class SensorMonitor:
    """Monitor and display tactile sensor data"""

    def __init__(self):
        self.parser = UsbPacketParser()
        self.serial_port: Optional[serial.Serial] = None
        self.running = False
        self.verbose = False  # Set to True for debug prints

        # Statistics
        self.total_packets = 0
        self.total_bytes = 0
        self.last_stats_time = time.time()
        self.packets_in_window = 0
        self.bytes_in_window = 0
        self.displays_in_window = 0  # Count complete data sets
        self.refresh_rate_hz = 0.0
        self.data_rate_kbs = 0.0  # KB/s (kilobytes per second)

        # Last update time
        self.last_update_time = 0
        self._display_initialized = False
        self._cursor_hidden = False
        self._alt_screen_enabled = False

        # Sensor info
        self.firmware_version = ""

        # Per-finger baseline for static tactile (28 taxels each)
        self.baseline = [[0] * 28 for _ in range(NUM_FINGERS)]


    def find_sensor(self) -> Optional[str]:
        """
        Find the tactile sensor device.
        First tries udev symlinks (Linux), then falls back to VID:PID matching.
        Returns the port name if found, None otherwise.
        """
        print("Searching for tactile sensor...")

        # 1. Try udev symlinks (Linux with rules installed)
        import os
        for i in range(10):
            symlink = f"/dev/rq_tsf85_{i}"
            if os.path.exists(symlink):
                print(f"Found sensor via udev symlink: {symlink}")
                return symlink

        # 2. Fallback: match by USB VID:PID
        vid_pid_pairs = [
            (MASTER_HUB_APP_VID, MASTER_HUB_APP_PID),
            (MASTER_HUB_APP_VID_OLD, MASTER_HUB_APP_PID_OLD),
        ]
        for p in serial.tools.list_ports.comports():
            for vid, pid in vid_pid_pairs:
                if p.vid == vid and p.pid == pid:
                    print(f"Found sensor via VID:PID match: {p.device}")
                    return p.device

        return None

    def connect(self, port_name: str) -> bool:
        """
        Connect to the sensor on the specified port.
        Returns True if successful, False otherwise.
        """
        try:
            print(f"Connecting to {port_name}...")
            self.serial_port = serial.Serial(
                port=port_name,
                baudrate=BAUD_RATE,
                bytesize=DATA_BITS,
                parity=PARITY,
                stopbits=STOP_BITS,
                timeout=TIMEOUT,
                write_timeout=TIMEOUT
            )

            # Set DTR and RTS (may help wake up the sensor)
            self.serial_port.dtr = True
            self.serial_port.rts = False

            print(f"Connected successfully at {BAUD_RATE} baud")
            print("Initializing sensor...")

            # Wait a bit for the port to stabilize
            time.sleep(0.2)

            # Clear any stale data
            self.serial_port.reset_input_buffer()
            self.serial_port.reset_output_buffer()
            self.parser.buffer.clear()

            print("Port initialized")

            # Read firmware verison
            self.read_firmware_version()

            return True

        except serial.SerialException as e:
            print(f"Failed to connect: {e}")
            return False

    def read_firmware_version(self):
        self.firmware_version = self.parser.print_firmware_version(self.serial_port)


    def start_autosend(self, period_ms: int = 1):
        """Start continuous sensor data streaming"""
        if not self.serial_port:
            return False

        print(f"Starting autosend mode (period={period_ms}ms)...")
        command = self.parser.create_autosend_command(period_ms)

        print(f"[DEBUG] Sending autosend command: {len(command)} bytes = {command.hex()}")
        bytes_written = self.serial_port.write(command)
        self.serial_port.flush()  # Ensure it's sent immediately
        print(f"[DEBUG] Wrote {bytes_written} bytes to serial port")

        time.sleep(0.2)  # Give sensor time to start
        print("Autosend command sent")
        return True

    def stop_autosend(self):
        """Stop continuous sensor data streaming"""
        if not self.serial_port:
            return

        print("\nStopping autosend...")
        command = self.parser.create_autosend_command(0)  # period=0 stops autosend
        self.serial_port.write(command)
        time.sleep(0.1)

    def update_statistics(self, num_packets: int, num_bytes: int):
        """Update data rate statistics"""
        self.total_packets += num_packets
        self.total_bytes += num_bytes
        self.packets_in_window += num_packets
        self.bytes_in_window += num_bytes

        current_time = time.time()
        elapsed = current_time - self.last_stats_time

        if elapsed >= REFRESH_RATE_WINDOW:
            # Calculate rates (matching TactileSensorUI calculations exactly)
            # Refresh rate = complete data sets per second (not packets per second)
            self.refresh_rate_hz = self.displays_in_window / elapsed

            # Data rate: bytes per second, displayed as KB/s
            # Matching TactileSensorUI: receivedBytes * 1000 / elapsed_ms = bytes/second
            bytes_per_second = self.bytes_in_window / elapsed
            self.data_rate_kbs = bytes_per_second / 1000.0  # Convert to KB/s

            # Reset window counters
            self.packets_in_window = 0
            self.bytes_in_window = 0
            self.displays_in_window = 0
            self.last_stats_time = current_time

    def format_tactile_grid(self, values: list) -> List[str]:
        """Format 28-value tactile grid as list of 7 strings (7 rows, 4 columns)"""
        if len(values) != 28:
            return ["Invalid data"]

        lines = []
        for row in range(7):
            row_values = values[row*4:(row+1)*4]
            line = " ".join(f"{val:5d}" for val in row_values)
            lines.append(line)
        return lines

    def display_sensor_data(self, data: SensorData):
        """Display sensor data in continuously updating format"""
        lines = []
        lines.append("=" * 80)
        lines.append(f"Robotiq Tactile Sensor Monitor, firmware version {self.firmware_version}".center(80))
        lines.append("=" * 80)
        lines.append(f"Data Rate: {self.data_rate_kbs:.3f} KB/s  |  "
                     f"Refresh Rate: {self.refresh_rate_hz:.1f} Hz  |  "
                     f"Total Packets: {self.total_packets}")
        lines.append("=" * 80)
        lines.append("")

        for finger_id in range(NUM_FINGERS):
            finger = data.fingers[finger_id]

            lines.append(f"FINGER {finger_id}")
            lines.append("-" * 80)

            # Static Tactile (7x4 grid)
            lines.append("  Static Tactile (7 rows × 4 columns):")
            # Subtract baseline from static tactile (element-wise)
            baseline_corrected = [s - b for s, b in zip(finger.static_tactile, self.baseline[finger_id])]
            for row in self.format_tactile_grid(baseline_corrected):
                lines.append("    " + row)
            lines.append("")

            # Dynamic Tactile
            lines.append(f"  Dynamic Tactile: {finger.dynamic_tactile:6d}")
            lines.append("")

            # IMU data
            lines.append(f"  Accelerometer: X={finger.accelerometer[0]:6d}  "
                         f"Y={finger.accelerometer[1]:6d}  "
                         f"Z={finger.accelerometer[2]:6d}")
            lines.append(f"  Gyroscope:     X={finger.gyroscope[0]:6d}  "
                         f"Y={finger.gyroscope[1]:6d}  "
                         f"Z={finger.gyroscope[2]:6d}")
            lines.append(f"  Timestamp: {finger.timestamp:6d}")
            lines.append("")

        lines.append("=" * 80)
        lines.append("Press Ctrl+C to exit")

        # Trim to terminal height to avoid scrolling when the display is taller than the window
        term_height = shutil.get_terminal_size((80, 50)).lines
        max_lines = max(term_height - 1, 1)  # leave a line for the final newline
        if len(lines) > max_lines:
            lines = lines[:max_lines - 1] + [
                f"... truncated (need {len(lines)} lines, available {max_lines})"
            ]

        # Render in place at the top of the screen to avoid scrolling noise
        if not self._display_initialized:
            # Switch to alternate screen to avoid scrollback growth
            sys.stdout.write("\033[?1049h")
            self._alt_screen_enabled = True

            sys.stdout.write("\033[?25l")  # Hide cursor for a cleaner view
            self._cursor_hidden = True
            self._display_initialized = True

        # Clear screen and move to top-left, then write the frame
        sys.stdout.write("\033[H\033[2J")
        sys.stdout.write("\n".join(lines) + "\n")
        sys.stdout.flush()

    def reset_baseline(self, num_samples: int = 1000):
        """
        Reset the baseline for all taxels by averaging static tactile data over num_samples.

        Args:
            num_samples: Number of data samples to average (default: 1000)
        """
        if not self.serial_port or not self.running:
            print("Error: Sensor must be connected and running to reset baseline")
            return False

        print(f"\nResetting baseline using {num_samples} samples...")
        print("Please ensure the sensor is not being touched.")

        # Initialize accumulator arrays for each finger's taxels
        # Structure: accumulators[finger_id][taxel_index] = sum of values
        accumulators = [[0] * 28 for _ in range(NUM_FINGERS)]
        samples_collected = 0

        # Collect samples
        start_time = time.time()
        timeout = 30  # 30 second timeout

        while samples_collected < num_samples:
            # Check timeout
            if time.time() - start_time > timeout:
                print(f"\nTimeout: Only collected {samples_collected}/{num_samples} samples")
                return False

            # Read available data
            waiting = self.serial_port.in_waiting
            if waiting > 0:
                data = self.serial_port.read(waiting)

                # Parse packets
                for packet in self.parser.feed_bytes(data):
                    new_data_available = self.parser.parse_sensor_packet(packet)
                    if not all(new_data_available[:NUM_FINGERS]):
                        continue
                    sensor_data = self.parser.get_sensor_data()
                    for f in sensor_data.fingers:
                        f.new_data_available = False

                    # Accumulate static tactile values for each finger
                    for finger_id in range(NUM_FINGERS):
                        finger = sensor_data.fingers[finger_id]
                        for taxel_idx in range(28):
                            accumulators[finger_id][taxel_idx] += finger.static_tactile[taxel_idx]

                    samples_collected += 1

                    # Progress indicator every 100 samples
                    if samples_collected % 100 == 0:
                        print(f"  Progress: {samples_collected}/{num_samples} samples collected")

            else:
                # Block in OS until data arrives; releases GIL without CPU spinning
                try:
                    select.select([self.serial_port.fd], [], [], 0.001)
                except (AttributeError, ValueError, OSError):
                    time.sleep(0.001)

        # Calculate averages and update baselines
        for finger_id in range(NUM_FINGERS):
            for taxel_idx in range(28):
                self.baseline[finger_id][taxel_idx] = accumulators[finger_id][taxel_idx] // num_samples

        print(f"\nBaseline reset complete! Collected {samples_collected} samples.")
        print("New baseline values set for all taxels.")
        return True

    def read_serial_data(self, callback):
        """Read serial data and invoke callback(sensor_data) for each complete frame."""
        while self.running:
            waiting = self.serial_port.in_waiting
            if waiting > 0:
                data = self.serial_port.read(waiting)
                for packet in self.parser.feed_bytes(data):
                    new_data_available = self.parser.parse_sensor_packet(packet)
                    if all(new_data_available[:NUM_FINGERS]):
                        sensor_data = self.parser.get_sensor_data()
                        for f in sensor_data.fingers:
                            f.new_data_available = False
                        callback(sensor_data)
            else:
                try:
                    select.select([self.serial_port.fd], [], [], 0.001)
                except (AttributeError, ValueError, OSError):
                    time.sleep(0.001)

    def run(self):
        """Main monitoring loop"""
        self.running = True
        self.last_stats_time = time.time()

        print("Starting sensor monitoring...")
        print("Reading data...\n")

        # Debug counters
        bytes_received_total = 0
        reads_with_data = 0
        reads_without_data = 0
        try:
            while self.running:
                # Read available data
                waiting = self.serial_port.in_waiting
                if waiting > 0:
                    data = self.serial_port.read(waiting)
                    bytes_received_total += len(data)
                    reads_with_data += 1

                    # Debug: Print first time we receive data
                    if self.verbose and reads_with_data == 1:
                        print(f"[DEBUG] First data received: {len(data)} bytes")
                        print(f"[DEBUG] First few bytes (hex): {data[:min(20, len(data))].hex()}")

                    # Parse packets
                    packets = self.parser.feed_bytes(data)

                    if packets:
                        self.update_statistics(len(packets), len(data))

                        # Debug: Print first packet info
                        if self.verbose and self.total_packets == len(packets):
                            print(f"[DEBUG] First packet parsed! Total packets found: {len(packets)}")
                            print(f"[DEBUG] First packet length: {len(packets[0])} bytes")

                        for packet in packets:
                            new_data_available = self.parser.parse_sensor_packet(packet)
                            sensor_data = self.parser.get_sensor_data()
                            if all(new_data_available[:NUM_FINGERS]):
                                for f in sensor_data.fingers:
                                    f.new_data_available = False
                                self.display_sensor_data(sensor_data)
                                self.displays_in_window += 1
                                self.last_update_time = time.time()
                else:
                    reads_without_data += 1

                    # Debug: Print status every 100 empty reads
                    if self.verbose and reads_without_data % 100 == 0:
                        print(f"[DEBUG] Status: {reads_with_data} reads with data, "
                              f"{bytes_received_total} total bytes, "
                              f"{self.total_packets} packets parsed")

                    # Block in OS until data arrives; releases GIL without CPU spinning
                    try:
                        select.select([self.serial_port.fd], [], [], 0.001)
                    except (AttributeError, ValueError, OSError):
                        time.sleep(0.001)

        except KeyboardInterrupt:
            print("\n\nShutdown requested...")
        finally:
            self.running = False

    def cleanup(self):
        """Cleanup resources"""
        self.stop_autosend()

        # Ensure cursor is visible again
        if self._cursor_hidden:
            sys.stdout.write("\033[?25h")
            sys.stdout.flush()

        if self._alt_screen_enabled:
            sys.stdout.write("\033[?1049l")
            sys.stdout.flush()

        if self.serial_port and self.serial_port.is_open:
            print("Closing serial port...")
            self.serial_port.close()

        print("Cleanup complete")



# Debugging classes
class TrackingParser(UsbPacketParser):
    """
    UsbPacketParser subclass that counts individual field arrivals and collects
    firmware timestamp deltas per finger, without changing any parsing logic.
    """

    def __init__(self):
        super().__init__()
        # (sensor_type, finger_id) -> arrival count in current window
        self.field_counts: Dict[Tuple[int, int], int] = defaultdict(int)
        # raw timestamp delta list per finger (firmware ticks, uint16 wraps at 65535)
        self.ts_deltas: List[List[int]] = [[], []]
        self._ts_prev = [None, None]

    def snapshot_and_reset(self) -> Tuple[Dict, List[List[int]]]:
        """Atomically snapshot then clear the window counters."""
        counts = dict(self.field_counts)
        deltas = [list(d) for d in self.ts_deltas]
        self.field_counts = defaultdict(int)
        self.ts_deltas = [[], []]
        return counts, deltas

    def parse_sensor_packet(self, packet: bytes) -> bool:
        """
        Pre-scan the packet to count field arrivals and accumulate timestamp
        deltas per finger, then delegate actual parsing to the parent.
        """
        if len(packet) < USB_PACKET_HEADER_SIZE:
            return False

        data = packet[USB_PACKET_HEADER_SIZE:]
        data_length = len(data)
        idx = 0

        while idx < data_length:
            sensor_byte = data[idx]
            sensor_type = sensor_byte & 0xF0
            finger_id   = (sensor_byte >> 2) & 0x03
            idx += 1

            field_size = _FIELD_BYTE_SIZES.get(sensor_type)
            if field_size is None or idx + field_size > data_length:
                break

            if finger_id < NUM_FINGERS:
                self.field_counts[(sensor_type, finger_id)] += 1

            # Accumulate timestamp deltas per finger (uint64, big-endian)
            if sensor_type == SENSOR_TYPE_TIMESTAMP and finger_id < NUM_FINGERS:
                ts = (
                    (data[idx]     << 56) | (data[idx + 1] << 48) |
                    (data[idx + 2] << 40) | (data[idx + 3] << 32) |
                    (data[idx + 4] << 24) | (data[idx + 5] << 16) |
                    (data[idx + 6] << 8)  |  data[idx + 7]
                )
                prev = self._ts_prev[finger_id]
                if prev is not None and ts > prev:
                    self.ts_deltas[finger_id].append(ts - prev)
                self._ts_prev[finger_id] = ts

            idx += field_size

        new_data_available = super().parse_sensor_packet(packet)
        # Reset new_data_available flags after reading so counts stay per-packet
        for f in self.sensor_data.fingers:
            f.new_data_available = False
        return new_data_available



# ── Monitor Packets Seperately────────────────────────────────────────────────────────────────────
class FieldTracker:

    def __init__(self):
        self.parser       = TrackingParser()
        self.serial_port: Optional[serial.Serial] = None
        self.running      = False
        self.firmware_version = ""

        # Running totals
        self.total_packets = 0
        self.total_bytes   = 0
        self.frames_in_window   = 0

        # Bytes/packets in current window (for data rate)
        self.packets_in_window = 0
        self.bytes_in_window   = 0
        self.last_stats_time   = time.time()

        # Displayed stats (updated each window)
        self.field_rates: Dict[Tuple[int, int], float] = {}
        self.frames_hz   = 0.0
        self.data_rate_kbs  = 0.0
        self.window_elapsed = 0.0
        self.ts_stats: List[Optional[Dict]] = [None, None]
        self.lost_packets_window: List[int] = [0, 0]  # gaps >1ms in last window
        self.lost_packets_total: List[int]  = [0, 0]  # running total

        self._display_initialized = False
        self._cursor_hidden       = False
        self._alt_screen_enabled  = False

    # ── Connection helpers (mirrors Monitor class) ─────────────────────────
    def _find_port(self) -> Optional[str]:
        # 1. Try udev symlinks (Linux with rules installed)
        for i in range(10):
            symlink = f"/dev/rq_tsf85_{i}"
            if os.path.exists(symlink):
                print(f"Found sensor via udev symlink: {symlink}")
                return symlink

        # 2. Fallback: match by USB VID:PID
        for vid, pid in [(MASTER_HUB_APP_VID, MASTER_HUB_APP_PID),
                         (MASTER_HUB_APP_VID_OLD, MASTER_HUB_APP_PID_OLD)]:
            for p in serial.tools.list_ports.comports():
                if p.vid == vid and p.pid == pid:
                    print(f"Found sensor on {p.device}  (VID:{vid:#06x} PID:{pid:#06x})")
                    return p.device
        return None

    def connect(self, port: str) -> bool:
        try:
            self.serial_port = serial.Serial(
                port=port, baudrate=BAUD_RATE, bytesize=DATA_BITS,
                parity=PARITY, stopbits=STOP_BITS,
                timeout=TIMEOUT, write_timeout=TIMEOUT,
            )
            self.serial_port.dtr = True
            self.serial_port.rts = False
            time.sleep(0.2)
            self.serial_port.reset_input_buffer()
            self.serial_port.reset_output_buffer()

            # Request firmware version
            self.serial_port.write(self.parser.create_get_firmware_command())
            self.serial_port.flush()
            deadline = time.time() + 1.0
            while time.time() < deadline:
                raw = self.serial_port.read(64)
                if not raw:
                    continue
                for pkt in self.parser.feed_bytes(raw):
                    if len(pkt) >= USB_PACKET_HEADER_SIZE and pkt[2] == USB_COMMAND_GET_VERSION:
                        self.firmware_version = pkt[USB_PACKET_HEADER_SIZE:].decode(
                            'ascii', errors='replace').strip('\x00').strip()
                        break
                if self.firmware_version:
                    break

            print(f"Connected to {port}  firmware: {self.firmware_version or '(unknown)'}")
            return True
        except serial.SerialException as e:
            print(f"Failed to connect: {e}")
            return False

    def start_autosend(self) -> bool:
        if not self.serial_port:
            return False
        cmd = self.parser.create_autosend_command(1)
        self.serial_port.write(cmd)
        self.serial_port.flush()
        time.sleep(0.2)
        return True

    def stop_autosend(self):
        if self.serial_port:
            self.serial_port.write(self.parser.create_autosend_command(0))
            time.sleep(0.1)

    # ── Stats update ──────────────────────────────────────────────────────────
    def _update_stats(self, num_packets: int, num_bytes: int):
        self.total_packets     += num_packets
        self.total_bytes       += num_bytes
        self.packets_in_window += num_packets
        self.bytes_in_window   += num_bytes

        now     = time.time()
        elapsed = now - self.last_stats_time

        if elapsed >= REFRESH_RATE_WINDOW:
            counts, deltas = self.parser.snapshot_and_reset()

            # Field rates
            self.field_rates = {key: cnt / elapsed for key, cnt in counts.items()}

            # Frame complete rate
            self.frames_hz = self.frames_in_window / elapsed

            # Data rate
            self.data_rate_kbs = (self.bytes_in_window / elapsed) / 1000.0

            # Timestamp delta stats per finger
            self.ts_stats = [_delta_stats(deltas[fi]) for fi in range(NUM_FINGERS)]

            # Lost packets: any gap > 1ms between consecutive timestamps
            window_lost = [
                sum(1 for d in deltas[fi] if d > 1)
                for fi in range(NUM_FINGERS)
            ]
            self.lost_packets_window = window_lost
            self.lost_packets_total  = [
                self.lost_packets_total[fi] + window_lost[fi]
                for fi in range(NUM_FINGERS)
            ]

            self.window_elapsed = elapsed

            # Reset window
            self.packets_in_window = 0
            self.bytes_in_window   = 0
            self.frames_in_window  = 0
            self.last_stats_time   = now

    # ── Display ───────────────────────────────────────────────────────────────
    def _render(self):
        W = 72
        lines = []
        lines.append("=" * W)
        lines.append(f"Robotiq Modality Rate Tracker   fw: {self.firmware_version}".center(W))
        lines.append("=" * W)
        lines.append(
            f"Data Rate: {self.data_rate_kbs:.2f} KB/s  |  "
            f"Frames: {self.frames_hz:.1f} Hz  |  "
            f"Packets: {self.total_packets}"
        )
        lines.append(f"Window: {self.window_elapsed:.3f} s")
        lines.append("=" * W)
        lines.append("")

        # ── Per-field rates table ─────────────────────────────────────────────
        lines.append(f"  {'Field':<20}  {'F0 (Hz)':>10}  {'F1 (Hz)':>10}  {'Match':>6}")
        lines.append("  " + "-" * (W - 2))
        for stype in FIELD_ORDER:
            label = FIELD_LABEL[stype]
            hz0 = self.field_rates.get((stype, 0), 0.0)
            hz1 = self.field_rates.get((stype, 1), 0.0)
            # Flag if either finger is >5% below the other
            if hz0 > 0 and hz1 > 0:
                ratio = min(hz0, hz1) / max(hz0, hz1)
                flag = "OK" if ratio >= 0.95 else "DIFF"
            elif hz0 == 0 and hz1 == 0:
                flag = "----"
            else:
                flag = "MISS"
            lines.append(f"  {label:<20}  {hz0:>10.1f}  {hz1:>10.1f}  {flag:>6}")

        lines.append("")
        lines.append(f"  {'Frame Complete':<20}  {self.frames_hz:>10.1f}")
        lines.append("")
        lines.append("=" * W)
        lines.append("")

        # ── Timestamp delta table ─────────────────────────────────────────────
        lines.append(f"  {'Timestamp Deltas (ms)':24}  {'F0':>12}  {'F1':>12}")
        lines.append("  " + "-" * (W - 2))
        stat_rows = [
            ("Mean",    "mean",  ".1f"),
            ("Std Dev", "std",   ".1f"),
            ("Min",     "min",   ".0f"),
            ("Max",     "max",   ".0f"),
            ("Count",   "count", "d"),
        ]
        for row_label, key, fmt in stat_rows:
            vals = []
            for fi in range(NUM_FINGERS):
                s = self.ts_stats[fi]
                if s is None:
                    vals.append("  --")
                elif fmt == "d":
                    vals.append(f"{s[key]:>12d}")
                else:
                    vals.append(f"{s[key]:>12{fmt}}")
            lines.append(f"  {row_label:<24}  {vals[0]}  {vals[1]}")

        lines.append("  " + "-" * (W - 2))
        lw = self.lost_packets_window
        lt = self.lost_packets_total
        lines.append(
            f"  {'Lost pkts this window':<24}  {lw[0]:>12d}  {lw[1]:>12d}"
        )
        lines.append(
            f"  {'Lost pkts total':<24}  {lt[0]:>12d}  {lt[1]:>12d}"
        )

        lines.append("")
        lines.append("=" * W)
        lines.append("Press Ctrl+C to exit")

        # Trim to terminal height
        th = shutil.get_terminal_size((80, 50)).lines
        if len(lines) > th - 1:
            lines = lines[:th - 2] + [f"... truncated ({len(lines)} lines needed)"]

        if not self._display_initialized:
            sys.stdout.write("\033[?1049h")
            self._alt_screen_enabled = True
            sys.stdout.write("\033[?25l")
            self._cursor_hidden = True
            self._display_initialized = True

        sys.stdout.write("\033[H\033[2J")
        sys.stdout.write("\n".join(lines) + "\n")
        sys.stdout.flush()

    # ── Main loop ─────────────────────────────────────────────────────────────
    def run(self):
        self.running = True
        self.last_stats_time = time.time()

        try:
            while self.running:
                waiting = self.serial_port.in_waiting
                if waiting > 0:
                    raw = self.serial_port.read(waiting)
                    packets = self.parser.feed_bytes(raw)

                    if packets:
                        self._update_stats(len(packets), len(raw))
                        for packet in packets:
                            new_data_available = self.parser.parse_sensor_packet(packet)
                            if all(new_data_available[:NUM_FINGERS]):
                                self.frames_in_window += 1
                                self._render()
                else:
                    try:
                        select.select([self.serial_port.fd], [], [], 0.001)
                    except (AttributeError, ValueError, OSError):
                        time.sleep(0.001)

        except KeyboardInterrupt:
            pass
        finally:
            self.running = False

    def cleanup(self):
        self.stop_autosend()
        if self._cursor_hidden:
            sys.stdout.write("\033[?25h")
            sys.stdout.flush()
        if self._alt_screen_enabled:
            sys.stdout.write("\033[?1049l")
            sys.stdout.flush()
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()

def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description="Robotiq Tactile Sensor Monitor")
    parser.add_argument('--web', action='store_true',
                        help='Launch web-based visualization in browser')
    parser.add_argument('--port', type=int, default=8080,
                        help='Web server port (default: 8080)')
    parser.add_argument('--debug', type=bool, default=False,
                    help='Toggle Sensor outputs vs feedback debugger')
    args = parser.parse_args()

    print("=" * 80)
    print("Simple Tactile Sensor Check Tool".center(80))
    print("=" * 80)
    print()

    if args.debug:
        tracker = FieldTracker()

        def _sig(sig, frame):
            tracker.running = False
        signal.signal(signal.SIGINT, _sig)

        port = tracker._find_port()
        if not port:
            print("Sensor not found. Check USB connection.")
            for p in serial.tools.list_ports.comports():
                vid = p.vid or 0
                pid = p.pid or 0
                print(f"  {p.device}: {p.description}  VID:PID={vid:04X}:{pid:04X}")
            return 1

        if not tracker.connect(port):
            return 1

        if not tracker.start_autosend():
            tracker.cleanup()
            return 1

        print("Streaming — display starts after first 1-second window...")

        try:
            tracker.run()
        finally:
            tracker.cleanup()

        print("\nDone.")
        return 0

    else:
        monitor = SensorMonitor()

        # Set up signal handler for graceful shutdown
        def signal_handler(sig, frame):
            monitor.running = False

        signal.signal(signal.SIGINT, signal_handler)

        # Find sensor
        port = monitor.find_sensor()
        if not port:
            print("\nError: Tactile sensor not found!")
            print("\nTroubleshooting:")
            print("1. Check that the sensor is plugged in")
            print("2. On Linux, check udev rules are installed")
            print("3. On Windows, ensure USB drivers are installed")
            print("\nAvailable ports:")
            for p in serial.tools.list_ports.comports():
                vid = f"{p.vid:04X}" if p.vid is not None else "----"
                pid = f"{p.pid:04X}" if p.pid is not None else "----"
                print(f"  {p.device}: {p.description} (VID:PID={vid}:{pid})")
            return 1

        print()

        # Connect
        if not monitor.connect(port):
            return 1

        print()

        # Start streaming
        if not monitor.start_autosend(period_ms=1):
            monitor.cleanup()
            return 1

        print()

        # Set running flag (needed for reset_baseline)
        monitor.running = True

        # Calculate baseline before starting display
        print("Calibrating baseline...")
        if not monitor.reset_baseline(num_samples=1000):
            print("Warning: Baseline calibration failed, continuing with zero baseline")

        print()

        # Run monitoring loop
        try:
            if args.web:
                from web_viewer import run_web_viewer
                run_web_viewer(monitor, port=args.port)
            else:
                monitor.run()
        finally:
            monitor.cleanup()

        print("\nExiting...")
        return 0


if __name__ == "__main__":
    sys.exit(main())
