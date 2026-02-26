#!/usr/bin/env python3
"""
Data Recording Script for Robotiq Tactile Sensor
Records sensor data to CSV file with interactive start/stop control
"""

import sys
import os
import time
import csv
import argparse

from protocol import NUM_FINGERS
from quick_connect import SensorMonitor


def _kbhit_init():
    """Set up non-blocking keyboard input (cross-platform)."""
    if os.name == 'nt':
        import msvcrt
        return {'type': 'nt'}
    else:
        import tty
        import termios
        import select
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        tty.setcbreak(fd)
        return {'type': 'posix', 'fd': fd, 'old_settings': old_settings}


def _kbhit_check(ctx):
    """Check if a key has been pressed (non-blocking). Returns char or None."""
    if ctx['type'] == 'nt':
        import msvcrt
        if msvcrt.kbhit():
            return msvcrt.getch().decode('utf-8', errors='replace').lower()
    else:
        import select
        if select.select([sys.stdin], [], [], 0)[0]:
            return sys.stdin.read(1).lower()
    return None


def _kbhit_cleanup(ctx):
    """Restore terminal settings."""
    if ctx['type'] == 'posix':
        import termios
        termios.tcsetattr(ctx['fd'], termios.TCSADRAIN, ctx['old_settings'])


def collect_baseline(monitor, num_samples=1000):
    """
    Collect and average samples to establish a per-taxel baseline.

    Returns:
        List of baselines per finger (list of list of ints), or None on failure.
    """
    print(f"Collecting baseline ({num_samples} samples)...")
    print("Please ensure the sensor is not being touched.")

    accumulators = [[0] * 28 for _ in range(NUM_FINGERS)]
    samples_collected = 0
    start_time = time.time()
    timeout = 10

    while samples_collected < num_samples:
        if time.time() - start_time > timeout:
            print(f"\nWarning: Timeout - only collected {samples_collected}/{num_samples} samples")
            if samples_collected < 10:
                return None
            break

        for sensor_data in monitor.poll_data():
            for finger_id in range(NUM_FINGERS):
                finger = sensor_data.fingers[finger_id]
                for taxel_idx in range(28):
                    accumulators[finger_id][taxel_idx] += finger.static_tactile[taxel_idx]
            samples_collected += 1
            if samples_collected % 100 == 0:
                print(f"  Progress: {samples_collected}/{num_samples}", end='\r')

        time.sleep(0.001)

    baselines = []
    for finger_id in range(NUM_FINGERS):
        baselines.append([acc // samples_collected for acc in accumulators[finger_id]])

    print(f"\nBaseline collected successfully ({samples_collected} samples).")
    return baselines


def create_data_row(sensor_data, baselines, remove_baseline):
    """Create a data row from sensor data."""
    row = []

    # Timestamp (absolute system time in milliseconds since epoch)
    row.append(int(time.time() * 1000))

    # Dynamic tactile for each finger
    for finger_id in range(NUM_FINGERS):
        row.append(sensor_data.fingers[finger_id].dynamic_tactile)

    # Static tactile data for each finger
    for finger_id in range(NUM_FINGERS):
        finger = sensor_data.fingers[finger_id]
        if remove_baseline and baselines:
            corrected = [s - b for s, b in zip(finger.static_tactile, baselines[finger_id])]
            row.extend(corrected)
        else:
            row.extend(finger.static_tactile)

    # Accelerometer for each finger
    for finger_id in range(NUM_FINGERS):
        row.extend(sensor_data.fingers[finger_id].accelerometer)

    # Gyroscope for each finger
    for finger_id in range(NUM_FINGERS):
        row.extend(sensor_data.fingers[finger_id].gyroscope)

    return row


def create_baseline_row(baselines):
    """Create the baseline reference row (first row in CSV)."""
    row = []
    row.append(int(time.time() * 1000))

    # Dynamic tactile placeholders
    for _ in range(NUM_FINGERS):
        row.append(0)

    # Baseline static tactile values
    for finger_id in range(NUM_FINGERS):
        row.extend(baselines[finger_id])

    # IMU placeholders
    for _ in range(NUM_FINGERS):
        row.extend([0, 0, 0])  # accelerometer
    for _ in range(NUM_FINGERS):
        row.extend([0, 0, 0])  # gyroscope

    return row


def save_to_csv(filename, data):
    """Save recorded data to CSV file."""
    with open(filename, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile, delimiter=';')

        # Write header
        header = ['Time(ms)']

        for finger_id in range(NUM_FINGERS):
            header.append(f'D0_{finger_id}')

        for finger_id in range(NUM_FINGERS):
            for taxel_id in range(28):
                header.append(f'S{taxel_id}_{finger_id}')

        for finger_id in range(NUM_FINGERS):
            header.extend([f'Ax{finger_id}', f'Ay{finger_id}', f'Az{finger_id}'])

        for finger_id in range(NUM_FINGERS):
            header.extend([f'Gx{finger_id}', f'Gy{finger_id}', f'Gz{finger_id}'])

        writer.writerow(header)

        for row in data:
            writer.writerow(row)


def record_loop(monitor, filename, remove_baseline):
    """Main recording loop with interactive control."""
    print("\n" + "=" * 60)
    print("Interactive Recording Mode")
    print("=" * 60)
    print(f"Output file: {filename}")
    print(f"Baseline removal: {'Enabled' if remove_baseline else 'Disabled'}")
    print("Commands:")
    print("  s - Start recording (collects baseline first)")
    print("  c - Stop recording and save")
    print("  q - Quit")
    print("=" * 60 + "\n")

    kb_ctx = _kbhit_init()
    recording = False
    recorded_data = []
    baselines = None

    try:
        while True:
            key = _kbhit_check(kb_ctx)
            if key:
                if key == 's' and not recording:
                    print("\n>>> COLLECTING BASELINE <<<")
                    baselines = collect_baseline(monitor)
                    if not baselines:
                        print("Failed to collect baseline. Please try again.")
                        continue

                    # Save baseline as first row
                    recorded_data = [create_baseline_row(baselines)]
                    recording = True
                    print(">>> RECORDING STARTED <<<")

                elif key == 'c' and recording:
                    print("\n>>> RECORDING STOPPED <<<")
                    recording = False
                    if len(recorded_data) > 1:
                        save_to_csv(filename, recorded_data)
                        print(f"Data saved to: {filename}")
                        print(f"Total samples: {len(recorded_data) - 1}")
                    else:
                        print("No data recorded")

                elif key == 'q':
                    if recording and len(recorded_data) > 1:
                        save_to_csv(filename, recorded_data)
                        print(f"Data saved to: {filename}")
                    print("\nExiting...")
                    break

            # Read sensor data
            for sensor_data in monitor.poll_data():
                if recording:
                    row = create_data_row(sensor_data, baselines, remove_baseline)
                    recorded_data.append(row)

                    if len(recorded_data) % 100 == 0:
                        print(f"Recorded {len(recorded_data) - 1} samples...", end='\r')

            time.sleep(0.001)

    except KeyboardInterrupt:
        if recording and len(recorded_data) > 1:
            save_to_csv(filename, recorded_data)
            print(f"\nData saved to: {filename}")
        print("\nInterrupted by user")
    finally:
        _kbhit_cleanup(kb_ctx)


def main():
    arg_parser = argparse.ArgumentParser(
        description='Record Robotiq tactile sensor data to CSV file',
        epilog="Example: python record_data.py output.csv"
    )
    arg_parser.add_argument('filename', help='Output CSV filename')
    arg_parser.add_argument('--keep-baseline', action='store_true',
                            help='Keep raw values (default: subtract baseline from static tactile)')
    args = arg_parser.parse_args()

    print("=" * 60)
    print("Robotiq Tactile Sensor Data Recorder")
    print("=" * 60)

    monitor = SensorMonitor()

    if not monitor.connect_to_sensor():
        return 1

    print("Initializing sensor...")
    time.sleep(1)
    print("Ready to record.\n")

    try:
        record_loop(monitor, args.filename,
                     remove_baseline=not args.keep_baseline)
    finally:
        monitor.cleanup()

    return 0


if __name__ == "__main__":
    sys.exit(main())
