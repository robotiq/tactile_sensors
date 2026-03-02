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
from typing import Optional, List

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: pyserial not installed")
    print("Please install with: pip install pyserial")
    sys.exit(1)

from protocol import UsbPacketParser, SensorData, FingerData

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
                    if not self.parser.parse_sensor_packet(packet):
                        continue
                    sensor_data = self.parser.get_sensor_data()

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
                    if self.parser.parse_sensor_packet(packet):
                        callback(self.parser.get_sensor_data())
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
                            if not self.parser.parse_sensor_packet(packet):
                                continue
                            sensor_data = self.parser.get_sensor_data()
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
