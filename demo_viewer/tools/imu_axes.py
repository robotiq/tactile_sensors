"""
Work out how the fingertip IMUs are actually mounted.

The viewer assumes which IMU axes lie in the plane a finger swings through and
which one is the axis it turns about (TIP_IN_PLANE_AXES and TIP_ROTATION_AXIS in
web_viewer.py). That mounting is not documented anywhere, so until it is checked
against real hardware it is a guess — and a wrong guess shows up as fingertip
angles that swing wildly and are marked invalid.

This measures it instead. Run it, follow the two prompts, and paste the
constants it prints into web_viewer.py.

    python3 tools/imu_axes.py            passive: just reads gravity
    python3 tools/imu_axes.py --motion   needs someone at the gripper

Passively, a static gravity reading says which IMU axis points up or down, which
is enough to rule an axis out. To pin it down outright someone has to move the
gripper: a rotation about an axis leaves that axis alone, so while the gripper
is rocked about the axis its fingers pivot around, the gyroscope axis it turns
about carries the signal and the accelerometer component along it barely changes
while the other two swing.
"""

import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from quick_connect import SensorMonitor  # noqa: E402
from protocol import NUM_FINGERS  # noqa: E402
import web_viewer  # noqa: E402

AXES = "xyz"
STILL_SECONDS = 3.0
FLEX_SECONDS = 10.0


def collect(monitor, seconds, label):
    """Gather accelerometer and gyroscope samples for a while."""
    samples = [[] for _ in range(NUM_FINGERS)]
    deadline = time.monotonic() + seconds

    def on_frame(data):
        for f in range(NUM_FINGERS):
            finger = data.fingers[f]
            samples[f].append((list(finger.accelerometer), list(finger.gyroscope)))

    import threading
    stop = threading.Event()

    def reader():
        monitor.read_serial_data(lambda d: None if stop.is_set() else on_frame(d))

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    while time.monotonic() < deadline:
        left = deadline - time.monotonic()
        print(f"\r  {label}: {left:4.1f}s ", end="", flush=True)
        time.sleep(0.1)
    stop.set()
    monitor.running = False
    print("\r" + " " * 40 + "\r", end="")
    return samples


def mean(values):
    return sum(values) / len(values) if values else 0.0


def stdev(values):
    if len(values) < 2:
        return 0.0
    m = mean(values)
    return math.sqrt(sum((v - m) ** 2 for v in values) / len(values))


def report_still(samples):
    print("Holding still — where gravity sits in each IMU's own axes:\n")
    for f, finger in enumerate(samples):
        if not finger:
            print(f"  finger {f}: no samples")
            continue
        accel = [mean([s[0][i] for s in finger]) for i in range(3)]
        norm = math.hypot(*accel)
        g = norm / web_viewer.ACCEL_LSB_PER_G
        share = [a / norm for a in accel] if norm else [0, 0, 0]
        parts = "  ".join(f"{AXES[i]}={accel[i]:8.0f} ({share[i]:+.2f} g)" for i in range(3))
        print(f"  finger {f}: {parts}")
        print(f"             |a| = {norm:.0f} counts = {g:.2f} g"
              + ("" if 0.9 < g < 1.1 else "   <-- not 1 g; is it moving?"))
    print()


def report_flex(samples):
    print("While flexing — which axis each finger turns about:\n")
    verdict = []
    for f, finger in enumerate(samples):
        if len(finger) < 50:
            print(f"  finger {f}: too few samples")
            verdict.append(None)
            continue
        gyro_rms = [math.sqrt(mean([s[1][i] ** 2 for s in finger])) for i in range(3)]
        accel_sd = [stdev([s[0][i] for s in finger]) for i in range(3)]

        axis = max(range(3), key=lambda i: gyro_rms[i])
        quiet = min(range(3), key=lambda i: accel_sd[i])
        print(f"  finger {f}: gyro rms   " +
              "  ".join(f"{AXES[i]}={gyro_rms[i]:7.0f}" for i in range(3)))
        print(f"             accel sd   " +
              "  ".join(f"{AXES[i]}={accel_sd[i]:7.0f}" for i in range(3)))
        print(f"             turns about {AXES[axis]}"
              + ("" if axis == quiet
                 else f"   (but accelerometer says {AXES[quiet]} — see below)"))
        verdict.append(axis if axis == quiet else None)
    print()
    return verdict


def interpret_still(samples):
    """Say what a static gravity reading already rules in or out."""
    print("What that means:\n")
    for f, finger in enumerate(samples):
        if not finger:
            continue
        accel = [mean([s[0][i] for s in finger]) for i in range(3)]
        norm = math.hypot(*accel)
        if not norm:
            continue
        share = [abs(a) / norm for a in accel]
        vertical = max(range(3), key=lambda i: share[i])
        if share[vertical] > 0.9:
            others = [AXES[i] for i in range(3) if i != vertical]
            print(f"  finger {f}: gravity is almost entirely on {AXES[vertical]} "
                  f"({share[vertical]:.2f}), so IMU {AXES[vertical]} points up or down.")
            print(f"             If the gripper is upright, the finger turns about "
                  f"{others[0]} or {others[1]},")
            print(f"             never {AXES[vertical]} — the current "
                  f"TIP_ROTATION_AXIS = {web_viewer.TIP_ROTATION_AXIS} "
                  f"({AXES[web_viewer.TIP_ROTATION_AXIS]}) "
                  + ("is wrong." if web_viewer.TIP_ROTATION_AXIS == vertical
                     else "is at least possible."))
        else:
            print(f"  finger {f}: gravity is spread across axes "
                  f"({', '.join(f'{AXES[i]}={share[i]:.2f}' for i in range(3))}),")
            print("             so the gripper is not sitting square to gravity. "
                  "That is fine,")
            print("             but the rotation axis cannot be inferred from it "
                  "alone.")
    print()


def main():
    motion = "--motion" in sys.argv
    monitor = SensorMonitor()
    port = monitor.find_sensor()
    if not port:
        print("Tactile sensor not found.")
        return 1
    if not monitor.connect(port) or not monitor.start_autosend(period_ms=1):
        return 1

    # read_serial_data loops on this; it starts False.
    monitor.running = True

    print("\n" + "=" * 72)
    print("Reading gravity — nobody needs to touch anything, the gripper just")
    print("has to be sitting still.")
    print("=" * 72)
    still = collect(monitor, STILL_SECONDS, "measuring")
    report_still(still)
    interpret_still(still)

    if not motion:
        print("Run again with --motion when someone can put hands on the gripper:")
        print("that pins the rotation axis outright instead of inferring it.")
        monitor.cleanup()
        return 0

    print("=" * 72)
    print("Now rock the whole gripper back and forth about the axis its fingers")
    print("pivot around — the way they swing when it opens and closes. A few")
    print("degrees is not enough; make it obvious.")
    print("=" * 72)
    time.sleep(2.0)
    monitor.running = True
    flex = collect(monitor, FLEX_SECONDS, "keep rocking")
    axes = report_flex(flex)

    print("=" * 72)
    agreed = [a for a in axes if a is not None]
    if agreed and all(a == agreed[0] for a in agreed) and len(agreed) == NUM_FINGERS:
        axis = agreed[0]
        in_plane = tuple(i for i in range(3) if i != axis)
        print("Both fingers agree. Put these in web_viewer.py:\n")
        print(f"    TIP_IN_PLANE_AXES = {in_plane}   # IMU "
              f"{AXES[in_plane[0]]} and {AXES[in_plane[1]]} span the linkage plane")
        print(f"    TIP_ROTATION_AXIS = {axis}        # IMU {AXES[axis]} is the "
              f"finger's rotation axis")
        print("\nIf the angles then move the wrong way, flip TIP_ANGLE_SIGN.")
    else:
        print("Inconclusive. The gyroscope and accelerometer disagree, or the two")
        print("fingers disagree, which usually means the fingertips were barely")
        print("moved or the whole gripper moved with them. Try again, flexing only")
        print("the fingertips and holding the gripper body still.")
    print("=" * 72)

    monitor.cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
