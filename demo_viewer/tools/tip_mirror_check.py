"""Drive the real estimator with two mirrored IMUs and a symmetric grasp."""
import math, sys, types
sys.path.insert(0, '/home/marbegi/robotiq/devel/tactile_sensors/demo_viewer')
for m in ('websockets', 'websockets.server', 'websockets.exceptions'):
    sys.modules.setdefault(m, types.ModuleType(m))
sys.modules['websockets'].serve = None
sys.modules['websockets.exceptions'].ConnectionClosed = Exception
import web_viewer as W

def Ry(p):
    c,s=math.cos(p),math.sin(p); return [[c,0,s],[0,1,0],[-s,0,c]]
def mul(M,v): return [sum(M[i][j]*v[j] for j in range(3)) for i in range(3)]
def dot(a,b): return sum(x*y for x,y in zip(a,b))

LSB = W.ACCEL_LSB_PER_G
G   = [0.0, 0.0, 1.0]
# One of the four candidate mountings; the sign result held for all four.
LEFT  = ([0,1,0], [0,0,1], [1,0,0])
RIGHT = tuple([-a[0], -a[1], a[2]] for a in LEFT)      # 180 deg about world +z
MOUNT = (LEFT, RIGHT)

def accel(f, phi):
    return [dot(G, mul(Ry(phi), a)) * LSB for a in MOUNT[f]]

buf = W.SensorDataBuffer()
DT = 0.001
t = 0.0
def feed(phi_left, phi_right, seconds):
    global t
    for _ in range(int(seconds / DT)):
        t += DT
        for f, phi in ((0, phi_left), (1, phi_right)):
            buf._update_tip_angle(f, accel(f, phi), [0, 0, 0], t)

feed(0.0, 0.0, 1.0)                       # startup calibration, tips at zero
angles, valid = buf.get_tip_snapshot()
print(f"  at rest:            F0 {angles[0]:+6.2f}  F1 {angles[1]:+6.2f}   valid={valid}")

failures = []
for deg in (5, 10, 20, 35):
    th = math.radians(deg)
    feed(th, -th, 1.0)                    # symmetric: left +phi, right -phi
    angles, valid = buf.get_tip_snapshot()
    agree = abs(angles[0] - angles[1]) < 0.05
    print(f"  symmetric {deg:2d} deg:    F0 {angles[0]:+6.2f}  F1 {angles[1]:+6.2f}"
          f"   {'agree' if agree else 'DISAGREE'}   valid={valid}")
    if not agree: failures.append(f"symmetric {deg} deg read {angles}")
    if not all(valid): failures.append(f"symmetric {deg} deg marked invalid")

# Asymmetric: only the left tip moves. The fingers must NOT track together.
feed(math.radians(25), 0.0, 1.0)
angles, _ = buf.get_tip_snapshot()
print(f"\n  left tip only:      F0 {angles[0]:+6.2f}  F1 {angles[1]:+6.2f}")
if abs(angles[1]) > 0.05:
    failures.append(f"still finger moved: {angles[1]}")
if abs(abs(angles[0]) - 25) > 0.05:
    failures.append(f"moved finger read {angles[0]}, expected 25")

print("\n" + ("FAILED: " + "; ".join(failures) if failures else "MIRROR CHECKS PASSED"))
sys.exit(1 if failures else 0)
