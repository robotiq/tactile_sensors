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
from ft_source import FTSource  # noqa: E402

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


# Where a fingertip pad sits at the fully-open pose, and what it turns about,
# in the gripper frame in mm (see gripper_geometry.js / build_gripper_geometry).
PAD_CENTRE_MM = (45.68, 0.0, 130.32)
DISTAL_PIVOT_MM = (67.76, 0.0, 98.33)


class SimulatedFingertipForce(FTSource):
    """A force pressed onto one fingertip, as the FT sensor would feel it.

    Everything the sensor reports at its own origin follows from one contact:
    `M = r x F`. Drawing that back out should put the force's line of action
    on the fingertip it is being applied to, which is the whole point of the
    visualisation — so the simulator is built to make that check meaningful.

    The contact point rides with the fingertip as it flexes. Only the distal
    rotation is applied, not the few millimetres the pivot itself travels; the
    exact line-of-action maths is checked separately against exact points.
    """

    def __init__(self, monitor, finger=0, rate_hz=100.0, peak_n=25.0, twist_nm=0.4):
        self.monitor = monitor
        self.finger = finger
        self.rate_hz = rate_hz
        self.peak_n = peak_n
        self.twist_nm = twist_nm

    def contact_point_m(self):
        """Contact point in the gripper frame, in metres, at the current pose."""
        inward = math.radians(self.monitor.current_inward[self.finger])
        # Inward flex turns the +x fingertip towards -x, i.e. negatively about y.
        angle = -inward if self.finger == 0 else inward
        side = 1.0 if self.finger == 0 else -1.0
        px, _, pz = DISTAL_PIVOT_MM
        cx, _, cz = PAD_CENTRE_MM
        dx, dz = (cx - px), (cz - pz)
        rx = dx * math.cos(angle) + dz * math.sin(angle)
        rz = -dx * math.sin(angle) + dz * math.cos(angle)
        return ((px + rx) * side / 1000.0, 0.0, (pz + rz) / 1000.0)

    def read(self, callback):
        period = 1.0 / self.rate_hz
        n = 0
        next_t = time.monotonic()
        while True:
            # A press that builds and releases, so the low-force fallback and
            # the line of action both get exercised.
            press = 0.5 - 0.5 * math.cos(n / 260.0)
            force = (0.0, 0.0, -self.peak_n * press)
            twist = self.twist_nm * math.sin(n / 170.0)

            contact = self.contact_point_m()
            origin = [v / 1000.0 for v in web_viewer.FT_ORIGIN_MM]
            r = [contact[i] - origin[i] for i in range(3)]
            moment = [r[1] * force[2] - r[2] * force[1],
                      r[2] * force[0] - r[0] * force[2],
                      r[0] * force[1] - r[1] * force[0]]
            # A twist about the force direction: the part of the moment no
            # translation can remove, and the only thing the curved arrow shows.
            moment[2] += twist

            callback(time.monotonic(), tuple(force) + tuple(moment))
            n += 1
            next_t += period
            time.sleep(max(0.0, next_t - time.monotonic()))


class FakeMonitor:
    """Stands in for SensorMonitor: same baseline attribute and read loop."""

    def __init__(self, tip_sweep_deg=25.0, hold_deg=None, tilt_deg=0.0):
        self.baseline = [[0] * 28 for _ in range(NUM)]
        self.tip_sweep_deg = tip_sweep_deg
        self.hold_deg = hold_deg
        self.tilt_deg = tilt_deg
        self._last_inward = [0.0] * NUM
        self._last_time = [None] * NUM
        # Shared with the simulated force source, so the contact point rides
        # with the fingertip instead of floating in space.
        self.current_inward = [0.0] * NUM

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
                self.current_inward[f] = inward
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
    parser.add_argument("--force-finger", type=int, choices=(0, 1), default=0,
                        help="which fingertip the simulated force presses on")
    parser.add_argument("--peak-force", type=float, default=25.0,
                        help="peak of the simulated press in N (default: 25)")
    parser.add_argument("--no-force", action="store_true",
                        help="no force/torque source, as if the sensor were absent")
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()

    if args.no_browser:
        webbrowser.open = lambda *a, **k: None

    monitor = FakeMonitor(tip_sweep_deg=args.tip_sweep, hold_deg=args.tip_hold,
                          tilt_deg=args.tilt)
    ft_source = None if args.no_force else SimulatedFingertipForce(
        monitor, finger=args.force_finger, peak_n=args.peak_force)

    web_viewer.run_web_viewer(monitor, port=args.port, ft_source=ft_source,
                              open_browser=not args.no_browser)


if __name__ == "__main__":
    main()
