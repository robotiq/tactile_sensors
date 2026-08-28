"""Knock a fingertip hard and check the estimate stays inside the joint's travel."""
import math, sys, types
sys.path.insert(0, '/home/marbegi/robotiq/devel/tactile_sensors/demo_viewer')
for m in ('websockets', 'websockets.server', 'websockets.exceptions'):
    sys.modules.setdefault(m, types.ModuleType(m))
sys.modules['websockets'].serve = None
sys.modules['websockets.exceptions'].ConnectionClosed = Exception
import web_viewer as W

LSB, GLSB = W.ACCEL_LSB_PER_G, W.GYRO_LSB_PER_DPS
DT = 0.001
buf = W.SensorDataBuffer()
t = 0.0
UP = [0.0, LSB, 0.0]                       # gripper upright, tip at zero

def feed(accel, gyro, seconds):
    global t
    for _ in range(int(seconds / DT)):
        t += DT
        buf._update_tip_angle(0, accel, gyro, t)
    return buf.get_tip_snapshot()[0][0]

feed(UP, [0, 0, 0], 1.0)                   # calibrate
fails = []

# A knock: gyro pegged at full scale, accelerometer swamped so the gravity
# reference is rejected and the filter runs open-loop on the gyro alone.
for rate_dps, seconds in ((250, 0.05), (250, 0.5), (-250, 0.5)):
    peak = 0.0
    for _ in range(int(seconds / DT)):
        t += DT
        buf._update_tip_angle(0, [3 * LSB, 3 * LSB, 0], [int(rate_dps * GLSB), 0, 0], t)
        peak = max(peak, abs(buf.get_tip_snapshot()[0][0]))
    angle = buf.get_tip_snapshot()[0][0]
    free = rate_dps * seconds
    inside = W.TIP_ANGLE_MIN_DEG - 1e-9 <= angle <= W.TIP_ANGLE_MAX_DEG + 1e-9
    print(f"  {rate_dps:+5d} dps for {seconds:4.2f}s (unclamped would reach "
          f"{free:+7.1f} deg): {angle:6.2f} deg  {'in range' if inside else 'OUT OF RANGE'}")
    if not inside: fails.append(f"{rate_dps} dps -> {angle}")

# Anti-windup: after a knock that would have wound the state to +125 deg, the
# tip must follow gravity straight back, not sit pinned while it unwinds.
back = feed(UP, [0, 0, 0], 0.25)
print(f"\n  released, tip physically back at 0: {back:.2f} deg "
      f"{'(recovered)' if abs(back) < 0.5 else '(STILL PINNED)'}")
if abs(back) >= 0.5: fails.append(f"did not recover: {back}")

# And ordinary motion inside the travel is untouched.
def tilt(deg):
    # Whichever physical direction this is, it is the one the estimator reports
    # positive; which of the two is "inward" is the open polarity question.
    r = math.radians(deg)
    return [0.0, math.cos(r) * LSB, math.sin(r) * LSB]
print()
for deg in (5, 20, 33, 45):
    got = feed(tilt(deg), [0, 0, 0], 1.0)
    want = min(deg, W.TIP_ANGLE_MAX_DEG)
    ok = abs(got - want) < 0.05
    print(f"  tip held at {deg:2d} deg -> {got:5.2f}  (expected {want:4.1f})"
          f"  {'' if ok else 'WRONG'}")
    if not ok: fails.append(f"{deg} deg -> {got}")

print("\n" + ("FAILED: " + "; ".join(fails) if fails else "LIMIT CHECKS PASSED"))
sys.exit(1 if fails else 0)
