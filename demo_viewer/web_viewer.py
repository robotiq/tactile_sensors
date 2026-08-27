"""
Web-based visualization server for Robotiq Tactile Sensor data.
Serves a real-time dashboard via WebSocket + HTTP.
"""

import asyncio
import json
import math
import os
import signal
import sys
import threading
import time
import traceback
import webbrowser
from collections import deque
from http.server import SimpleHTTPRequestHandler, HTTPServer
from functools import partial
from pathlib import Path

import websockets

from protocol import NUM_FINGERS
DISPLAY_POINTS = 300  # max points sent to browser for time-series
                      # (every stream is sent on every frame now, so keep it modest)
BROADCAST_HZ = 5      # display refresh rate

# --- Fingertip angle estimation -------------------------------------------
#
# The gripper view draws a 2F-85 held fully open with the fingers pointing up.
# In an encompassing grip the distal phalanx is a free DOF, so its angle cannot
# be derived from the gripper's opening — but the fingertip IMU can see it: the
# accelerometer measures the direction of gravity in the fingertip's own frame,
# and with the palm pointing up (+z vertical) a rotation of the fingertip about
# the finger axis shows up directly as a rotation of that gravity vector.
#
# Only the direction of the accelerometer vector is used, never its magnitude,
# so the accelerometer's scale factor does not enter the angle at all.

TIP_CAL_SAMPLES = 200        # samples averaged at startup to fix the zero
TIP_ACCEL_TOLERANCE = 0.10   # reject gravity beyond +-10% of its calibrated norm
TIP_AXIS_TOLERANCE = 0.30    # |g . rotation axis| above this => assumptions broken
# Time constant of the complementary filter: how long the gyro is trusted
# before the accelerometer has pulled the estimate back. Expressed as a time
# rather than a per-sample weight so the behaviour does not change with the
# sample rate.
TIP_FILTER_TAU_S = 0.05

# Which IMU axes lie in the plane the finger rotates in, and which one is the
# rotation axis itself.
#
# Measured on hardware (tools/imu_axes.py): with the gripper upright and still,
# gravity sits at 1.00 g on IMU y on both fingers. So y points along the palm's
# vertical and cannot be the axis a finger turns about, which is horizontal —
# it is x or z.
#
# Which of the two is still open: telling them apart needs the gripper actually
# rotating, and gravity alone cannot do it. Either choice gives a stable, valid
# angle while the gripper is still, and they differ only once a fingertip moves.
# Run `tools/imu_axes.py --motion` when someone can rock the gripper, and if the
# angle then moves the wrong way, flip TIP_ANGLE_SIGN.
TIP_IN_PLANE_AXES = (1, 2)   # IMU y and z span the linkage plane
TIP_ROTATION_AXIS = 0        # IMU x is the finger's rotation axis (provisional)
# Sign per finger. A positive reported angle always means the fingertip has
# rotated *inward*, towards the other finger — the direction the distal phalanx
# wraps in an encompassing grip — so a symmetric grasp reads the same on both
# fingers. The entries are opposite because the two fingertips are mirror
# images while their IMUs are mounted identically, so the same physical inward
# motion turns the two sensors in opposite directions. Mirroring for the
# drawing is the viewer's job, not this estimate's.
TIP_ANGLE_SIGN = (1.0, -1.0)

# Sensor scales. The IMU is an ICM-20948, configured for +-2 g and +-250 dps,
# and what reaches the viewer is its raw int16 counts: unscaled, unbiased, in
# the chip's own axes, with no mounting matrix applied. Sensitivity is therefore
# 32768 / full-scale. Accelerometer and gyroscope arrive in a single register
# burst, which is why they share an axis convention below.
ACCEL_LSB_PER_G = 32768.0 / 2.0     # 16384; only used for reporting, not the angle
GYRO_LSB_PER_DPS = 32768.0 / 250.0  # 131.072

# --- Force/torque -----------------------------------------------------------
#
# The FT sensor mounts between the robot flange and the gripper coupling, so its
# origin sits below the gripper's base_link. This offset is approximate: the
# adapter is 11 mm, but the sensor's own stack height is not documented in any
# repo to hand. It only shifts where the wrench is anchored in the drawing.
FT_ORIGIN_MM = [0.0, 0.0, -20.0]

# Older readings than this are stale — a disconnected sensor should stop drawing
# a wrench rather than leave the last one frozen on screen.
FT_STALE_AFTER_S = 0.5


class SensorDataBuffer:
    """Thread-safe circular buffers for sensor data."""

    def __init__(self):
        self._lock = threading.Lock()
        self.static_tactile = [None] * NUM_FINGERS
        self.dynamic_tactile = [deque(maxlen=4096) for _ in range(NUM_FINGERS)]
        self.baseline = [[0] * 28 for _ in range(NUM_FINGERS)]
        self.use_baseline = True
        self.adaptive_range = True
        self.default_range = 3000.0
        self.max_range = [300.0] * NUM_FINGERS  # adaptive starts from 0
        self.push_total = [0] * NUM_FINGERS
        self.push_corrupt = [0] * NUM_FINGERS
        # Fingertip angle state, per finger
        self.tip_angle = [0.0] * NUM_FINGERS        # degrees
        self.tip_valid = [False] * NUM_FINGERS
        self._tip_cal_sum = [[0.0, 0.0, 0.0] for _ in range(NUM_FINGERS)]
        self._tip_cal_count = [0] * NUM_FINGERS
        self._tip_zero_angle = [None] * NUM_FINGERS  # radians, set by calibration
        self._tip_ref_norm = [None] * NUM_FINGERS    # 1 g in raw counts
        self._tip_last_time = [None] * NUM_FINGERS
        self._tip_raw_angle = [0.0] * NUM_FINGERS    # radians, in the IMU's own frame
        # Latest force/torque reading: (fx, fy, fz) N, (mx, my, mz) Nm
        self.wrench = None
        self.wrench_time = 0.0
        self.wrench_error = None

    def _update_tip_angle(self, f, accel, gyro, now):
        """Estimate one fingertip's angle from its IMU. Caller holds the lock."""
        ax, az = (accel[i] for i in TIP_IN_PLANE_AXES)
        norm = math.sqrt(sum(v * v for v in accel))
        if norm == 0.0:
            self.tip_valid[f] = False
            return

        # Startup calibration: assumptions 3 and 4 (fully open, pointing up)
        # say the true angle is zero right now, so whatever the IMU reports is
        # the mounting offset. Averaging also gives us 1 g in raw counts.
        if self._tip_zero_angle[f] is None:
            for i in range(3):
                self._tip_cal_sum[f][i] += accel[i]
            self._tip_cal_count[f] += 1
            if self._tip_cal_count[f] >= TIP_CAL_SAMPLES:
                mean = [v / self._tip_cal_count[f] for v in self._tip_cal_sum[f]]
                self._tip_zero_angle[f] = math.atan2(mean[TIP_IN_PLANE_AXES[1]],
                                                     mean[TIP_IN_PLANE_AXES[0]])
                ref_norm = math.sqrt(sum(v * v for v in mean))
                if ref_norm == 0.0:
                    # Nothing usable to reference against; start over rather
                    # than divide by it below.
                    self._tip_cal_sum[f] = [0.0, 0.0, 0.0]
                    self._tip_cal_count[f] = 0
                    return
                self._tip_ref_norm[f] = ref_norm
                self._tip_last_time[f] = now
            return

        # Gravity leaking onto the rotation axis means the gripper is not
        # pointing up, or the IMU is not mounted the way TIP_IN_PLANE_AXES
        # assumes. Either way the angle below would be a plausible-looking lie.
        off_plane = abs(accel[TIP_ROTATION_AXIS]) / norm
        quiescent = abs(norm / self._tip_ref_norm[f] - 1.0) <= TIP_ACCEL_TOLERANCE

        angle = math.atan2(az, ax) - self._tip_zero_angle[f]
        angle = (angle + math.pi) % (2 * math.pi) - math.pi

        last = self._tip_last_time[f]
        dt = now - last if last is not None else 0.0
        self._tip_last_time[f] = now
        if GYRO_LSB_PER_DPS and 0.0 < dt < 0.1:
            # Complementary filter: the gyro carries the fast motion, the
            # accelerometer the absolute reference — but only while it is
            # trustworthy, i.e. the finger is not being accelerated. Gyro and
            # accelerometer share an axis convention because they arrive in
            # one register burst, in the chip's own frame.
            rate = math.radians(gyro[TIP_ROTATION_AXIS] / GYRO_LSB_PER_DPS)
            predicted = self._tip_raw_angle[f] + rate * dt
            alpha = TIP_FILTER_TAU_S / (TIP_FILTER_TAU_S + dt)
            angle = (alpha * predicted + (1.0 - alpha) * angle) if quiescent else predicted
        elif not quiescent:
            # No gyro to fall back on, and an accelerating finger has no usable
            # gravity reference: hold the last angle rather than track noise.
            self.tip_valid[f] = False
            return

        self._tip_raw_angle[f] = angle
        self.tip_angle[f] = math.degrees(angle) * TIP_ANGLE_SIGN[f]
        self.tip_valid[f] = off_plane <= TIP_AXIS_TOLERANCE

    def push(self, sensor_data):
        with self._lock:
            now = time.monotonic()
            if sensor_data.fingers[0].timestamp != 0 and self.default_range != 1200.0:
                self.default_range = 1200.0
            for f in range(NUM_FINGERS):
                finger = sensor_data.fingers[f]
                st = list(finger.static_tactile)
                self.push_total[f] += 1
                if len(st) != 28:
                    self.push_corrupt[f] += 1
                    continue
                self.static_tactile[f] = st
                self.dynamic_tactile[f].append(finger.dynamic_tactile)
                # Estimated here, at the full sample rate: the browser only
                # ever sees the resulting angle, never the raw IMU stream.
                self._update_tip_angle(f, list(finger.accelerometer),
                                       list(finger.gyroscope), now)

    def get_static_snapshot(self):
        with self._lock:
            result = []
            for f in range(NUM_FINGERS):
                raw = self.static_tactile[f]
                if raw is None:
                    result.append([0] * 28)
                    continue
                if len(raw) != 28:
                    result.append([0] * 28)
                    continue
                if self.use_baseline:
                    values = [max(0, raw[i] - self.baseline[f][i]) for i in range(28)]
                else:
                    values = list(raw)
                if self.adaptive_range:
                    m = max(values) if values else 0
                    if m > self.max_range[f]:
                        self.max_range[f] = m
                result.append(values)
            return result, list(self.max_range)

    def push_wrench(self, values, now=None):
        with self._lock:
            self.wrench = list(values)
            self.wrench_time = now if now is not None else time.monotonic()

    def get_wrench_snapshot(self):
        """Latest force/torque, or None when there is nothing trustworthy."""
        with self._lock:
            if self.wrench is None:
                return None
            if time.monotonic() - self.wrench_time > FT_STALE_AFTER_S:
                return None
            return list(self.wrench)

    def get_tip_snapshot(self):
        """Return (angles in degrees, per-finger validity)."""
        with self._lock:
            return list(self.tip_angle), list(self.tip_valid)

    def get_dynamic_snapshot(self):
        """Return subsampled dynamic time-domain data."""
        with self._lock:
            dyn = []
            for f in range(NUM_FINGERS):
                dyn.append(_subsample_deque(self.dynamic_tactile[f], DISPLAY_POINTS))
            return dyn

    def reset_baseline(self):
        with self._lock:
            reset_val = 300.0 if self.adaptive_range else self.default_range
            for f in range(NUM_FINGERS):
                if self.static_tactile[f]:
                    self.baseline[f] = list(self.static_tactile[f])
                self.max_range[f] = reset_val


def _subsample_deque(d, max_points):
    n = len(d)
    if n == 0:
        return []
    if n <= max_points:
        return list(d)
    step = n / max_points
    return [d[int(i * step)] for i in range(max_points)]


class WebViewer:
    def __init__(self, monitor, port=8080, ft_source=None):
        self.monitor = monitor
        self.ft_source = ft_source
        self.port = port
        self.buffer = SensorDataBuffer()
        self.clients = set()
        self._had_client = False

    def serial_callback(self, sensor_data):
        try:
            self.buffer.push(sensor_data)
        except Exception:
            traceback.print_exc(file=sys.stderr)

    async def websocket_handler(self, websocket):
        self.clients.add(websocket)
        self._had_client = True
        try:
            async for message in websocket:
                msg = json.loads(message)
                if msg.get("type") == "reset_baseline":
                    self.buffer.reset_baseline()
                elif msg.get("type") == "set_raw_mode":
                    self.buffer.use_baseline = not msg.get("raw", False)
                elif msg.get("type") == "set_adaptive_range":
                    with self.buffer._lock:
                        self.buffer.adaptive_range = msg.get("adaptive", True)
                        if self.buffer.adaptive_range:
                            self.buffer.max_range = [300.0] * NUM_FINGERS
                        else:
                            self.buffer.max_range = [self.buffer.default_range] * NUM_FINGERS
        except websockets.ConnectionClosed:
            pass
        finally:
            self.clients.discard(websocket)
            if self._had_client and not self.clients:
                # Last client disconnected — give a brief grace period for
                # page refreshes, then shut down.
                await asyncio.sleep(2.0)
                if not self.clients:
                    print("\nAll clients disconnected. Shutting down...")
                    os._exit(0)

    async def broadcast_loop(self):
        interval = 1.0 / BROADCAST_HZ
        busy = set()  # clients still sending the previous frame
        last_diag = 0.0

        async def _send(client, payload):
            try:
                await client.send(payload)
            except websockets.ConnectionClosed:
                pass
            finally:
                busy.discard(client)

        while True:
            try:
                now = time.monotonic()
                if now - last_diag >= 5.0:
                    last_diag = now
                    b = self.buffer
                    with b._lock:
                        dyn_sizes = [len(b.dynamic_tactile[f]) for f in range(NUM_FINGERS)]
                        tips = [f"{b.tip_angle[i]:.1f}"
                                + ("" if b.tip_valid[i] else "?")
                                for i in range(NUM_FINGERS)]
                        corrupt = [
                            f"{b.push_corrupt[f]}/{b.push_total[f]}"
                            f" ({100*b.push_corrupt[f]/b.push_total[f]:.0f}%)"
                            if b.push_total[f] else "0/0"
                            for f in range(NUM_FINGERS)
                        ]
                    print(f"[diag] clients={len(self.clients)}  dyn={dyn_sizes}  "
                          f"tip={tips}  corrupt={corrupt}")
                if self.clients:
                    # Single-page viewer: send every stream on every frame.
                    values, max_ranges = self.buffer.get_static_snapshot()
                    tip_angles, tip_valid = self.buffer.get_tip_snapshot()
                    wrench = self.buffer.get_wrench_snapshot()
                    msg = {
                        "type": "data",
                        "wrench": wrench,
                        "ftOrigin": FT_ORIGIN_MM,
                        "wrenchError": self.buffer.wrench_error,
                        "static": values,
                        "maxRange": max_ranges,
                        "dynamic": self.buffer.get_dynamic_snapshot(),
                        "tipAngle": tip_angles,
                        "tipAngleValid": tip_valid,
                    }

                    payload = json.dumps(msg)
                    for client in self.clients.copy():
                        if client not in busy:
                            busy.add(client)
                            asyncio.ensure_future(_send(client, payload))
                        # else: client is still sending previous frame, drop this one
            except Exception:
                traceback.print_exc(file=sys.stderr)
            await asyncio.sleep(interval)

    async def run_server(self):
        web_dir = Path(__file__).parent / "web"
        handler = partial(QuietHTTPHandler, directory=str(web_dir))
        httpd = HTTPServer(("0.0.0.0", self.port), handler)
        http_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        http_thread.start()
        print(f"  HTTP server:      http://localhost:{self.port}")

        ws_port = self.port + 1
        async with websockets.serve(self.websocket_handler, "0.0.0.0", ws_port):
            print(f"  WebSocket server: ws://localhost:{ws_port}")
            await self.broadcast_loop()


class QuietHTTPHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def end_headers(self):
        # The page and its assets change while the viewer is being worked on,
        # and a browser that reuses a cached app.js or gripper_geometry.js just
        # looks like the change did not happen. Nothing here is worth caching.
        self.send_header("Cache-Control", "no-store, must-revalidate")
        super().end_headers()


def run_web_viewer(monitor, port=8080, ft_source=None, open_browser=True):
    viewer = WebViewer(monitor, port, ft_source)

    # Seed buffer baseline from the calibration done in main()
    for f in range(NUM_FINGERS):
        viewer.buffer.baseline[f] = list(monitor.baseline[f])

    serial_thread = threading.Thread(
        target=monitor.read_serial_data,
        args=(viewer.serial_callback,),
        daemon=True
    )
    serial_thread.start()

    if ft_source is not None:
        def read_ft():
            # The force/torque sensor is a separate device on a separate bus.
            # If it is missing or unplugged the gripper must keep working, so
            # the failure is recorded and shown, not raised.
            try:
                ft_source.read(lambda t, values: viewer.buffer.push_wrench(values, t))
            except Exception as exc:
                viewer.buffer.wrench_error = f"{type(exc).__name__}: {exc}"
                traceback.print_exc(file=sys.stderr)

        threading.Thread(target=read_ft, daemon=True).start()

    url = f"http://localhost:{port}"
    print(f"Web viewer starting...")
    print(f"  URL: {url}")
    print("  Press Ctrl+C to stop.\n")
    if open_browser:
        webbrowser.open(url)

    # All threads are daemon — hard exit on Ctrl+C is safe and responsive
    signal.signal(signal.SIGINT, lambda *_: os._exit(0))
    asyncio.run(viewer.run_server())
