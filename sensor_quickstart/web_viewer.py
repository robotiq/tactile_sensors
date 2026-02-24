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


NUM_FINGERS = 2
DISPLAY_POINTS = 500  # max points sent to browser for time-series
BROADCAST_HZ = 5      # display refresh rate


class SensorDataBuffer:
    """Thread-safe circular buffers for sensor data."""

    def __init__(self):
        self._lock = threading.Lock()
        self.static_tactile = [None] * NUM_FINGERS
        self.dynamic_tactile = [deque(maxlen=4096) for _ in range(NUM_FINGERS)]
        self.accelerometer = [deque(maxlen=2000) for _ in range(NUM_FINGERS)]
        self.gyroscope = [deque(maxlen=2000) for _ in range(NUM_FINGERS)]
        self.baseline = [[0] * 28 for _ in range(NUM_FINGERS)]
        self.use_baseline = True
        self.adaptive_range = True
        self.default_range = 3000.0
        self.max_range = [300.0] * NUM_FINGERS  # adaptive starts from 0

    def push(self, sensor_data):
        with self._lock:
            if sensor_data.timestamp_ms != 0 and self.default_range != 1200.0:
                self.default_range = 1200.0
            for f in range(NUM_FINGERS):
                finger = sensor_data.fingers[f]
                self.static_tactile[f] = list(finger.static_tactile)
                self.dynamic_tactile[f].append(finger.dynamic_tactile)
                self.accelerometer[f].append(list(finger.accelerometer))
                self.gyroscope[f].append(list(finger.gyroscope))

    def get_static_snapshot(self):
        with self._lock:
            result = []
            for f in range(NUM_FINGERS):
                raw = self.static_tactile[f]
                if raw is None:
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

    def get_dynamic_snapshot(self):
        """Return subsampled dynamic time-domain data."""
        with self._lock:
            dyn = []
            for f in range(NUM_FINGERS):
                dyn.append(_subsample_deque(self.dynamic_tactile[f], DISPLAY_POINTS))
            return dyn

    def get_imu_snapshot(self):
        """Return subsampled IMU data."""
        with self._lock:
            acc = []
            gyr = []
            for f in range(NUM_FINGERS):
                acc.append(_subsample_deque_3axis(self.accelerometer[f], DISPLAY_POINTS))
                gyr.append(_subsample_deque_3axis(self.gyroscope[f], DISPLAY_POINTS))
            return acc, gyr

    def compute_fft(self):
        """Compute FFT from dynamic buffer. Zero-pads to 4096 if >= 512 samples."""
        FFT_SIZE = 4096
        MIN_SAMPLES = 512
        # Copy data under the lock, compute FFT outside it
        with self._lock:
            snapshots = [
                list(self.dynamic_tactile[f])[-FFT_SIZE:]
                if len(self.dynamic_tactile[f]) >= MIN_SAMPLES else None
                for f in range(NUM_FINGERS)
            ]
        results = []
        for s in snapshots:
            if s is None:
                results.append(None)
            else:
                # Zero-pad to FFT_SIZE
                if len(s) < FFT_SIZE:
                    s = s + [0] * (FFT_SIZE - len(s))
                results.append(_fft_magnitudes(s))
        return results

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


def _subsample_deque_3axis(d, max_points):
    """Subsample deque of [x,y,z] lists, returning {x:[], y:[], z:[]}."""
    n = len(d)
    if n == 0:
        return {"x": [], "y": [], "z": []}
    if n <= max_points:
        indices = range(n)
    else:
        step = n / max_points
        indices = [int(i * step) for i in range(max_points)]
    x, y, z = [], [], []
    for i in indices:
        s = d[i]
        x.append(s[0])
        y.append(s[1])
        z.append(s[2])
    return {"x": x, "y": y, "z": z}


def _fft_magnitudes(real_data):
    """Pure-Python iterative Cooley-Tukey FFT. Returns first N/2 magnitude bins."""
    N = len(real_data)
    buf_re = list(map(float, real_data))
    buf_im = [0.0] * N
    # Bit-reversal permutation
    j = 0
    for i in range(1, N):
        bit = N >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            buf_re[i], buf_re[j] = buf_re[j], buf_re[i]
            buf_im[i], buf_im[j] = buf_im[j], buf_im[i]
    # Butterfly
    length = 2
    while length <= N:
        ang = -2.0 * math.pi / length
        w_re = math.cos(ang)
        w_im = math.sin(ang)
        half = length // 2
        for i in range(0, N, length):
            cur_re, cur_im = 1.0, 0.0
            for k in range(half):
                u_idx = i + k
                v_idx = i + k + half
                t_re = cur_re * buf_re[v_idx] - cur_im * buf_im[v_idx]
                t_im = cur_re * buf_im[v_idx] + cur_im * buf_re[v_idx]
                buf_re[v_idx] = buf_re[u_idx] - t_re
                buf_im[v_idx] = buf_im[u_idx] - t_im
                buf_re[u_idx] += t_re
                buf_im[u_idx] += t_im
                cur_re, cur_im = cur_re * w_re - cur_im * w_im, cur_re * w_im + cur_im * w_re
        length *= 2
    return [math.sqrt(buf_re[i] ** 2 + buf_im[i] ** 2) for i in range(N // 2)]


class WebViewer:
    def __init__(self, monitor, port=8080):
        self.monitor = monitor
        self.port = port
        self.buffer = SensorDataBuffer()
        self.clients = set()
        self.active_tab = "static"
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
                if msg.get("type") == "tab_change":
                    self.active_tab = msg["tab"]
                elif msg.get("type") == "reset_baseline":
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
            now = time.monotonic()
            if now - last_diag >= 5.0:
                last_diag = now
                b = self.buffer
                with b._lock:
                    dyn_sizes = [len(b.dynamic_tactile[f]) for f in range(NUM_FINGERS)]
                    acc_sizes = [len(b.accelerometer[f]) for f in range(NUM_FINGERS)]
                    gyr_sizes = [len(b.gyroscope[f]) for f in range(NUM_FINGERS)]
                print(f"[diag] tab={self.active_tab}  clients={len(self.clients)}  "
                      f"dyn={dyn_sizes}  accel={acc_sizes}  gyro={gyr_sizes}")
            if self.clients:
                tab = self.active_tab
                msg = {"type": "data", "tab": tab}

                if tab == "static":
                    values, max_ranges = self.buffer.get_static_snapshot()
                    msg["static"] = values
                    msg["maxRange"] = max_ranges
                elif tab == "dynamic":
                    msg["dynamic"] = self.buffer.get_dynamic_snapshot()
                elif tab == "imu":
                    acc, gyr = self.buffer.get_imu_snapshot()
                    msg["accel"] = acc
                    msg["gyro"] = gyr

                payload = json.dumps(msg)
                for client in self.clients.copy():
                    if client not in busy:
                        busy.add(client)
                        asyncio.ensure_future(_send(client, payload))
                    # else: client is still sending previous frame, drop this one
            await asyncio.sleep(interval)

    async def fft_loop(self):
        """Compute FFT at 1Hz and broadcast directly to clients."""
        loop = asyncio.get_event_loop()
        while True:
            fft_result = await loop.run_in_executor(None, self.buffer.compute_fft)
            if self.clients and self.active_tab == "dynamic":
                payload = json.dumps({"type": "fft", "fft": fft_result})
                await asyncio.gather(
                    *[c.send(payload) for c in self.clients.copy()],
                    return_exceptions=True
                )
            await asyncio.sleep(1.0)

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
            await asyncio.gather(self.broadcast_loop(), self.fft_loop())


class QuietHTTPHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass


def run_web_viewer(monitor, port=8080):
    viewer = WebViewer(monitor, port)

    serial_thread = threading.Thread(
        target=monitor.read_serial_data,
        args=(viewer.serial_callback,),
        daemon=True
    )
    serial_thread.start()

    # Wait for sensor data to arrive, then reset baseline so the
    # web page opens with a clean zero reference.
    time.sleep(0.5)
    viewer.buffer.reset_baseline()

    url = f"http://localhost:{port}"
    print(f"Web viewer starting...")
    print(f"  URL: {url}")
    print("  Press Ctrl+C to stop.\n")
    webbrowser.open(url)

    # All threads are daemon — hard exit on Ctrl+C is safe and responsive
    signal.signal(signal.SIGINT, lambda *_: os._exit(0))
    asyncio.run(viewer.run_server())
