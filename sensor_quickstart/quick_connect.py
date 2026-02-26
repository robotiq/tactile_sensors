#!/usr/bin/env python3
"""
Simple Tactile Sensor Check Tool
Detects, connects to, and displays data from Robotiq Tactile Sensors
"""

import sys
import time
import signal
import shutil
import argparse
from typing import Optional, List

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: pyserial not installed")
    print("Please install with: pip install pyserial")
    sys.exit(1)

from protocol import UsbPacketParser, SensorData, FingerData, NUM_FINGERS

# Serial port configuration
BAUD_RATE = 115200
DATA_BITS = 8
PARITY = 'N'
STOP_BITS = 1
TIMEOUT = 0.1  # 100ms timeout for reads

# Tactile sensor USB identifiers
# Newer production units use 0x16D0:0x14CC, older units use Cypress default 0x04B4:0xF232
SENSOR_VID_PID_PAIRS = [
    (0x16D0, 0x14CC),  # Robotiq (new)
    (0x04B4, 0xF232),  # Cypress default (old)
]

# Display configuration
REFRESH_RATE_WINDOW = 1.0  # Calculate refresh rate over 1 second


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

        # 2. Fallback: match by USB VID:PID (try each known pair)
        for p in serial.tools.list_ports.comports():
            for vid, pid in SENSOR_VID_PID_PAIRS:
                if p.vid == vid and p.pid == pid:
                    print(f"Found sensor via VID:PID match: {p.device}")
                    print(f"  (No udev symlink found — this is normal on Windows)")
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

            print("Port initialized")
            return True

        except serial.SerialException as e:
            print(f"Failed to connect: {e}")
            return False

    def start_autosend(self, period_ms: int = 1):
        """Start continuous sensor data streaming"""
        if not self.serial_port:
            return False

        print(f"Starting autosend mode (period={period_ms}ms)...")
        command = self.parser.create_autosend_command(period_ms)

        bytes_written = self.serial_port.write(command)
        self.serial_port.flush()  # Ensure it's sent immediately

        time.sleep(0.2)  # Give sensor time to start
        print("Autosend command sent")
        return True

    def connect_to_sensor(self, period_ms: int = 1) -> bool:
        """Find, connect, and start streaming from the sensor.

        Returns True if successful, False otherwise.
        """
        port = self.find_sensor()
        if not port:
            print("\nError: Tactile sensor not found!")
            print("\nAvailable ports:")
            for p in serial.tools.list_ports.comports():
                vid = f"{p.vid:04X}" if p.vid is not None else "----"
                pid = f"{p.pid:04X}" if p.pid is not None else "----"
                print(f"  {p.device}: {p.description} (VID:PID={vid}:{pid})")
            return False

        if not self.connect(port):
            return False

        self.parser.print_firmware_version(self.serial_port)

        if not self.start_autosend(period_ms=period_ms):
            self.cleanup()
            return False

        return True

    def poll_data(self) -> list:
        """Read available serial data and return list of complete SensorData snapshots.

        Non-blocking: returns an empty list if no data is available.
        """
        results = []
        waiting = self.serial_port.in_waiting
        if waiting > 0:
            data = self.serial_port.read(waiting)
            packets = self.parser.feed_bytes(data)
            self.update_statistics(len(packets), len(data))
            for packet in packets:
                if self.parser.parse_sensor_packet(packet):
                    results.append(self.parser.get_sensor_data())
        return results

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
            # Calculate rates (matching tactile_sensor_ui calculations exactly)
            # Refresh rate = complete data sets per second (not packets per second)
            self.refresh_rate_hz = self.displays_in_window / elapsed

            # Data rate: bytes per second, displayed as KB/s
            # Matching tactile_sensor_ui: receivedBytes * 1000 / elapsed_ms = bytes/second
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
        lines.append("Robotiq Tactile Sensor Monitor".center(80))
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
            for row in self.format_tactile_grid(finger.static_tactile):
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
            lines.append(f"  Magnetometer:  X={finger.magnetometer[0]:6d}  "
                         f"Y={finger.magnetometer[1]:6d}  "
                         f"Z={finger.magnetometer[2]:6d}")
            lines.append("")

            # Open Byte
            lines.append(f"  Open Byte: {finger.temperature:6d}")
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

    def read_serial_data(self, callback):
        """Read serial data in a loop, calling callback(sensor_data) on each complete dataset."""
        self.running = True
        self.last_stats_time = time.time()

        try:
            while self.running:
                for sensor_data in self.poll_data():
                    callback(sensor_data)
                    self.displays_in_window += 1
                    self.last_update_time = time.time()

                # Small sleep to prevent CPU spinning
                time.sleep(0.001)

        except KeyboardInterrupt:
            print("\n\nShutdown requested...")
        finally:
            self.running = False

    def run(self):
        """Main monitoring loop (terminal display)"""
        print("Starting sensor monitoring...")
        print("Reading data...\n")
        self.read_serial_data(callback=self.display_sensor_data)

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


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description="Robotiq Tactile Sensor Monitor")
    parser.add_argument('--web', action='store_true',
                        help='Launch web-based visualization in browser')
    parser.add_argument('--port', type=int, default=8080,
                        help='Web server port (default: 8080)')
    args = parser.parse_args()

    print("=" * 80)
    print("Simple Tactile Sensor Check Tool".center(80))
    print("=" * 80)
    print()

    monitor = SensorMonitor()

    # Set up signal handler for graceful shutdown
    def signal_handler(sig, frame):
        monitor.running = False

    signal.signal(signal.SIGINT, signal_handler)

    if not monitor.connect_to_sensor():
        return 1

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
