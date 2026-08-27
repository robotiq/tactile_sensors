"""
Run the web viewer against synthetic sensor data, with no hardware attached.

Useful for working on the dashboard itself: it feeds `run_web_viewer` the same
callback a real sensor would, with a moving pressure blob, a dynamic tactile
tone, and IMU data that mimics a fingertip on a 2F-85 held open and pointing up
(gravity along -z in the tip frame, rotated by a swept fingertip angle).

    python3 tools/simulate_sensor.py --port 8099
"""

import argparse
import math
import sys
import time
import webbrowser
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import protocol  # noqa: E402
import web_viewer  # noqa: E402

NUM = protocol.NUM_FINGERS
# Same scales the finger pad firmware programs into the ICM-20948.
ACCEL_LSB_PER_G = web_viewer.ACCEL_LSB_PER_G
GYRO_LSB_PER_DPS = web_viewer.GYRO_LSB_PER_DPS

# Hold still long enough for the viewer's startup tip calibration to finish
# before the fingertips start moving.
SETTLE_SAMPLES = 2 * web_viewer.TIP_CAL_SAMPLES


class FakeFinger:
    def __init__(self):
        self.timestamp = 0
        self.static_tactile = [0] * 28
        self.dynamic_tactile = 0
        self.accelerometer = [0, 0, 0]
        self.gyroscope = [0, 0, 0]


class FakeFrame:
    def __init__(self):
        self.fingers = [FakeFinger() for _ in range(NUM)]


class FakeMonitor:
    """Stands in for SensorMonitor: same baseline attribute and read loop."""

    def __init__(self, tip_sweep_deg=25.0, hold_deg=None, tilt_deg=0.0):
        self.baseline = [[0] * 28 for _ in range(NUM)]
        self.tip_sweep_deg = tip_sweep_deg
        self.hold_deg = hold_deg
        self.tilt_deg = tilt_deg
        self._last_inward = [0.0] * NUM
        self._last_time = [None] * NUM

    def tip_angle_deg(self, n, f):
        """Fingertip angle the simulated IMU should report.

        Always starts at zero: the viewer takes its first second of samples as
        the mounting reference, so a fingertip that is already deflected when
        the viewer starts is indistinguishable from one at rest.
        """
        if n < SETTLE_SAMPLES:
            return 0.0
        if self.hold_deg is not None:
            return self.hold_deg
        sweep = (1.0 - math.cos((n - SETTLE_SAMPLES) / 900.0)) / 2.0
        return self.tip_sweep_deg * sweep + 5.0 * f

    def _rate_counts(self, f, inward_deg):
        """Gyro reading consistent with how fast the fingertip is moving.

        The viewer fuses gyro with accelerometer, so an invented rate would
        fight the invented gravity vector. Differentiating the same angle keeps
        the two synthetic signals telling the same story.
        """
        now = time.monotonic()
        last_t = self._last_time[f]
        self._last_time[f] = now
        rate_dps = 0.0
        if last_t is not None and now > last_t:
            rate_dps = (inward_deg - self._last_inward[f]) / (now - last_t)
        self._last_inward[f] = inward_deg
        # Mirrored the same way the accelerometer is, since it is one chip.
        return rate_dps * (1.0 if f == 0 else -1.0) * GYRO_LSB_PER_DPS

    def read_serial_data(self, callback):
        n = 0
        while True:
            frame = FakeFrame()
            for f in range(NUM):
                finger = frame.fingers[f]
                finger.static_tactile = [
                    int(1500 * math.exp(
                        -(((i % 4) - 1.5 - f) ** 2
                          + ((i // 4) - 3 - 2 * math.sin(n / 300)) ** 2) / 3))
                    for i in range(28)
                ]
                finger.dynamic_tactile = int(8000 * math.sin(n / 7.0 + f)
                                             + 3000 * math.sin(n / 1.3))
                # Both fingertips carry the same sensor mounted the same way,
                # and the two fingers are mirror images: closing inward by the
                # same amount therefore turns the two IMUs opposite ways.
                inward = self.tip_angle_deg(n, f)
                angle = math.radians(inward * (1.0 if f == 0 else -1.0))
                # --tilt leans the whole gripper off vertical, putting gravity
                # on the finger's rotation axis: the angle then stops being
                # observable and the viewer should say so.
                tilt = math.radians(self.tilt_deg)
                in_plane = ACCEL_LSB_PER_G * math.cos(tilt)
                finger.accelerometer = [int(in_plane * math.sin(angle)),
                                        int(ACCEL_LSB_PER_G * math.sin(tilt)),
                                        int(-in_plane * math.cos(angle))]
                finger.gyroscope = [0, int(self._rate_counts(f, inward)), 0]
            callback(frame)
            n += 1
            if n % 50 == 0:
                time.sleep(0.005)


def main():
    parser = argparse.ArgumentParser(description="Run the web viewer on fake data")
    parser.add_argument("--port", type=int, default=8099, help="HTTP port (default: 8099)")
    parser.add_argument("--tip-sweep", type=float, default=25.0,
                        help="fingertip sweep amplitude in degrees (default: 25)")
    parser.add_argument("--tip-hold", type=float,
                        help="hold the fingertips at this angle instead of sweeping")
    parser.add_argument("--tilt", type=float, default=0.0,
                        help="lean the gripper this many degrees off vertical, to "
                             "exercise the 'angle not observable' path")
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()

    if args.no_browser:
        webbrowser.open = lambda *a, **k: None

    web_viewer.run_web_viewer(
        FakeMonitor(tip_sweep_deg=args.tip_sweep, hold_deg=args.tip_hold,
                    tilt_deg=args.tilt),
        port=args.port)


if __name__ == "__main__":
    main()
