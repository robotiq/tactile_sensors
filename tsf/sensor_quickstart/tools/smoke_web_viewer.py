"""
Smoke test of the web viewer with synthetic data, no hardware attached.

Starts `quick_connect.py --sim`, with its simulated gripper and force/torque
sensor, then checks what a browser needs:
- the page, every script, stylesheet and import-map module it names, and
  every module and data file the viewer's own scripts load, all return HTTP
  200;
- the websocket streams tactile data on every finger;
- the viewer exits with code 0 once the last client disconnects, and prints
  no traceback along the way.

Exits 0 when all of that holds, 1 otherwise. Run from anywhere:

    python3 tools/smoke_web_viewer.py
"""

import json
import os
import random
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from urllib.parse import urljoin

QUICKSTART = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(QUICKSTART))

# The viewer runs on this machine: never route its requests through a proxy.
os.environ["no_proxy"] = os.environ["NO_PROXY"] = "127.0.0.1,localhost"

from websockets.sync.client import connect  # noqa: E402

import protocol  # noqa: E402

START_TIMEOUT_S = 20.0
FRAME_TIMEOUT_S = 10.0
EXIT_TIMEOUT_S = 10.0

# What a module or a script names: static and dynamic imports, and data files.
JS_IMPORTS = re.compile(r"""(?:\bfrom|\bimport)\s*\(?\s*['"]([^'"]+)['"]""")
JS_DATA_FILES = re.compile(r"""['"]([\w./-]+\.(?:bin|json))['"]""")


# Below the ephemeral ranges of Linux (32768+), macOS and Windows (49152+):
# Windows hands out ephemeral ports in sequence, so a port next to one the
# system just picked would go to this test's own HTTP requests before the
# viewer could bind its websocket there.
PORT_RANGE = (20000, 32000)


def free_port_pair():
    """A port P with P+1 also free: the viewer serves HTTP on P, websocket on P+1.

    Probed on every interface, as the viewer binds them.
    """
    for _ in range(50):
        port = random.randrange(*PORT_RANGE)
        try:
            for p in (port, port + 1):
                with socket.socket() as s:
                    s.bind(("", p))
            return port
        except OSError:
            continue
    raise RuntimeError("no free pair of consecutive ports")


def get(url):
    """(status, body) of a GET; an HTTP error is a status, not an exception."""
    try:
        with urllib.request.urlopen(url, timeout=5) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as err:
        return err.code, b""


def page_assets(page, base):
    """URLs of the scripts, stylesheets and import-map modules the page names."""
    names = re.findall(r'<script[^>]*\bsrc="([^"]+)"', page)
    names += re.findall(r'<link[^>]*\bhref="([^"]+)"', page)
    import_map = {}
    for block in re.findall(r'<script type="importmap">(.*?)</script>', page, re.S):
        import_map.update(json.loads(block).get("imports", {}))
    names += import_map.values()
    urls = {urljoin(base + "/", n) for n in names if "://" not in n}
    return urls, {k: urljoin(base + "/", v) for k, v in import_map.items()}


def check_assets(page, base):
    """Fetch every file the page and the viewer's own modules load."""
    pending, import_map = page_assets(page, base)
    seen = set()
    while pending:
        url = pending.pop()
        if url in seen:
            continue
        seen.add(url)
        status, body = get(url)
        name = url[len(base) + 1:]
        if status != 200:
            raise AssertionError(f"{name}: HTTP {status}")
        # Third-party bundles are fetched, not crawled: only the viewer's own
        # modules say which files it needs.
        if url.endswith(".js") and "/vendor/" not in url:
            text = body.decode("utf-8", "replace")
            for spec in JS_IMPORTS.findall(text):
                pending.add(import_map.get(spec) or urljoin(url, spec))
            for data in JS_DATA_FILES.findall(text):
                pending.add(urljoin(base + "/", data))  # fetch() resolves against the page
    return sorted(u[len(base) + 1:] for u in seen)


def wait_for_page(base, viewer):
    deadline = time.monotonic() + START_TIMEOUT_S
    while time.monotonic() < deadline:
        if viewer.poll() is not None:
            raise AssertionError(f"viewer exited early with code {viewer.returncode}")
        try:
            status, body = get(base + "/")
        except OSError:  # not listening yet
            time.sleep(0.2)
            continue
        if status != 200:
            raise AssertionError(f"{base}/: HTTP {status}")
        return body.decode()
    raise AssertionError(f"no page at {base}/ after {START_TIMEOUT_S:.0f} s")


def open_websocket(ws_url, deadline):
    """Connect, retrying while refused: the viewer starts its websocket server
    after its HTTP server, so the page can answer before the websocket does."""
    while True:
        try:
            return connect(ws_url, open_timeout=5)
        except OSError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.2)


def check_frames(ws_url):
    """Read frames until every finger shows tactile data, not just zeros."""
    deadline = time.monotonic() + FRAME_TIMEOUT_S
    with open_websocket(ws_url, deadline) as ws:
        while time.monotonic() < deadline:
            try:
                msg = json.loads(ws.recv(timeout=max(0.1, deadline - time.monotonic())))
            except TimeoutError:
                break
            static = msg.get("static")
            if msg.get("type") != "data" or static is None:
                continue
            if len(static) != protocol.NUM_FINGERS:
                raise AssertionError(f"{len(static)} fingers in a frame, "
                                     f"expected {protocol.NUM_FINGERS}")
            if all(any(finger) for finger in static):
                return
    raise AssertionError(f"no tactile data on every finger within {FRAME_TIMEOUT_S:.0f} s")


def main():
    port = free_port_pair()
    base = f"http://127.0.0.1:{port}"
    # BROWSER=true: never open a browser. UTF-8: the viewer prints dashes,
    # which a Windows code page may not encode.
    env = dict(os.environ, PYTHONUNBUFFERED="1", PYTHONIOENCODING="utf-8",
               BROWSER="true")
    # A file, not a pipe: a viewer that prints more than a pipe holds would
    # block on its print, and fail as "no tactile data" instead of showing
    # what it printed.
    log = tempfile.TemporaryFile()
    viewer = subprocess.Popen(
        [sys.executable, "quick_connect.py", "--sim", "--port", str(port)],
        cwd=QUICKSTART, env=env, stdout=log, stderr=subprocess.STDOUT)

    def output():
        log.seek(0)
        return log.read().decode("utf-8", "replace")

    try:
        page = wait_for_page(base, viewer)
        files = check_assets(page, base)
        print(f"page and {len(files)} files: HTTP 200 ({', '.join(files)})")

        check_frames(f"ws://127.0.0.1:{port + 1}")
        print("websocket: tactile data on every finger")

        viewer.wait(timeout=EXIT_TIMEOUT_S)
        if viewer.returncode != 0:
            raise AssertionError(f"viewer exited with code {viewer.returncode}")
        if "Traceback" in output():
            raise AssertionError("the viewer printed a traceback")
        print("viewer exited cleanly after the last client left")
        return 0
    except Exception as exc:  # report, then show what the viewer printed
        print(f"FAIL: {exc}", file=sys.stderr)
        if viewer.poll() is None:
            viewer.kill()
        viewer.wait()
        print("--- viewer output ---", file=sys.stderr)
        print(output(), file=sys.stderr)
        return 1
    finally:
        if viewer.poll() is None:
            viewer.kill()
            viewer.wait()
        log.close()


if __name__ == "__main__":
    sys.exit(main())
