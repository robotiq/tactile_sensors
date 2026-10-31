"""
Run the web viewer against synthetic sensor data, with no hardware attached.

Useful for working on the dashboard itself: it feeds `run_web_viewer` the same
callback a real sensor would, with a moving pressure blob, a dynamic tactile
tone, and IMU data that mimics a fingertip on a 2F-85 held open and pointing up
(the accelerometer at -1 g on IMU y, as measured on hardware, turned in the y/z plane by a
swept fingertip angle).

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

    def __init__(self, tip_sweep_deg=25.0, hold_deg=None, tilt_deg=0.0,
                 upside_down=False):
        self.baseline = [[0] * 28 for _ in range(NUM)]
        # Fingers pointing down: gravity reversed in the fingertip's frame.
        # Fingers up reads -1 g on IMU y (measured); down reverses it.
        self.gravity_sign = 1 if upside_down else -1
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
        # Negated to match the accelerometer above, since it is one chip: the
        # gyro has to carry the rate in the frame the angle is expressed in, or
        # the complementary filter fuses two signals telling different stories.
        return -rate_dps * GYRO_LSB_PER_DPS

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
                # Upright and still, the accelerometer reads -1 g on IMU y on both
                # fingers (tools/imu_axes.py), and inward flex turns it in the
                # y/z plane -- the axes web_viewer's TIP_IN_PLANE_AXES names.
                #
                # Both fingers emit identical counts. The fingertips are mirror
                # images, so an inward turn is opposite in world terms; but the
                # right finger's IMU is the left one's turned 180 degrees about
                # that same vertical, so it sees that opposite turn about an
                # axis also pointing the opposite way. The two flips cancel,
                # which is why TIP_ANGLE_SIGN's entries are equal.
                inward = self.tip_angle_deg(n, f)
                self.current_inward[f] = inward
                # Negated because TIP_ANGLE_SIGN is -1: on the real gripper
                # inward flex turns the gravity vector negatively in this
                # plane, and the viewer flips it back on the way out.
                angle = math.radians(-inward)
                # --tilt leans the whole gripper off vertical, putting gravity
                # on the finger's rotation axis (IMU x): the angle then stops
                # being observable and the viewer should say so.
                tilt = math.radians(self.tilt_deg)
                in_plane = ACCEL_LSB_PER_G * math.cos(tilt) * self.gravity_sign
                finger.accelerometer = [int(ACCEL_LSB_PER_G * math.sin(tilt)),
                                        int(in_plane * math.cos(angle)),
                                        int(in_plane * math.sin(angle))]
                finger.gyroscope = [int(self._rate_counts(f, inward)), 0, 0]
            callback(frame)
            n += 1
            if n % 50 == 0:
                time.sleep(0.005)


class SimulatedGripper:
    """Stands in for pyrobotiqgripper.RobotiqGripper: the calls the viewer makes.

    Starts unactivated so the Activate button is exercised, moves at a rate set
    by the speed byte, and stops on a pretend object part-way through the
    closing stroke so the contact readout has something to show.
    """

    # The 2F-85 closes its full stroke in roughly 0.6 s at full speed, 4 s at the slowest.
    MIN_RATE, MAX_RATE = 60.0, 420.0   # position counts per second
    OBJECT_AT = 200                    # where the pretend object is met
    ACTIVATION_S = 1.5

    def __init__(self):
        self.com_port = "simulated"
        self._sta = 0
        self._pos = 0.0
        self._target = 0
        self._speed = 255
        self._force = 255
        self._obj = 3
        self._last = time.monotonic()

    def activate(self):
        time.sleep(self.ACTIVATION_S)
        self._sta, self._pos, self._target, self._obj = 3, 0.0, 0, 3
        self._last = time.monotonic()

    def move(self, position, speed=255, force=255, wait=True, readStatus=True,
             refreshStatus=False, start=False):
        if self._sta != 3:
            raise RuntimeError("gripper not activated")
        self.readStatus()
        self._target, self._speed, self._force = int(position), int(speed), int(force)

    def readStatus(self):
        now = time.monotonic()
        dt, self._last = now - self._last, now
        if self._sta != 3:
            return
        rate = self.MIN_RATE + (self.MAX_RATE - self.MIN_RATE) * self._speed / 255
        step = rate * dt
        goal = self._target
        if goal > self.OBJECT_AT and self._pos <= self.OBJECT_AT:
            goal = self.OBJECT_AT  # the object is in the way
        if abs(goal - self._pos) <= step:
            self._pos = float(goal)
            self._obj = 2 if goal != self._target else 3
        else:
            self._pos += step if goal > self._pos else -step
            self._obj = 0

    def status(self, refreshStatus=True):
        if refreshStatus:
            self.readStatus()
        moving_or_holding = self._obj in (0, 2)
        return {"gSTA": self._sta, "gPO": round(self._pos), "gPR": self._target,
                "gOBJ": self._obj, "gFLT": 0,
                # A grip draws current in proportion to the force setting.
                "gCU": (self._force // 4 if self._obj == 2 else 0)
                       + (5 if moving_or_holding else 0)}


def simulated_gripper():
    """A GripperController driving a SimulatedGripper, for run_web_viewer."""
    from gripper_control import GripperController
    sim = SimulatedGripper()
    return GripperController(sim, sim.com_port)


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
    parser.add_argument("--upside-down", action="store_true",
                        help="the gripper with its fingers pointing down")
    parser.add_argument("--force-finger", type=int, choices=(0, 1), default=0,
                        help="which fingertip the simulated force presses on")
    parser.add_argument("--peak-force", type=float, default=25.0,
                        help="peak of the simulated press in N (default: 25)")
    parser.add_argument("--no-force", action="store_true",
                        help="no force/torque source, as if the sensor were absent")
    parser.add_argument("--no-gripper", action="store_true",
                        help="no gripper controls, as if no gripper were connected")
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()

    if args.no_browser:
        webbrowser.open = lambda *a, **k: None

    monitor = FakeMonitor(tip_sweep_deg=args.tip_sweep, hold_deg=args.tip_hold,
                          tilt_deg=args.tilt, upside_down=args.upside_down)
    ft_source = None if args.no_force else SimulatedFingertipForce(
        monitor, finger=args.force_finger, peak_n=args.peak_force)

    web_viewer.run_web_viewer(monitor, port=args.port, ft_source=ft_source,
                              open_browser=not args.no_browser,
                              gripper=None if args.no_gripper else simulated_gripper())


if __name__ == "__main__":
    main()
