"""
Drive the gripper from the web viewer, through pyrobotiqgripper.

The gripper is a third device on its own USB/RS485 adapter, next to the tactile
sensor and the force/torque sensor. Like the FT sensor it is optional: a missing
gripper is reported and stepped over, and the viewer runs without the controls.

`tools/simulate_sensor.py` has a stand-in with the same few methods, so the
controls can be worked on with nothing plugged in.
"""

import sys
import threading
import time
import traceback

from serial_ports import usb_serial_ports

try:
    # gFLT codes that stop the gripper from acting on commands; the major ones
    # until it is reset and reactivated.
    from pyrobotiqgripper import GFLT_BLOCKING, GripperFaultError
except ImportError:
    # Not installed (it needs Python 3.10+). The simulated gripper still runs
    # through GripperController, and never faults.
    GFLT_BLOCKING = ()

    class GripperFaultError(Exception):
        pass

POLL_HZ = 10           # status refresh rate, and the rate slider moves are sent at
DEFAULT_SPEED = 128    # 0-255; the gripper's own default (255) is its fastest and hardest
DEFAULT_FORCE = 128
GSTA_ACTIVATED = 3     # gSTA once the activation routine has completed


def _clamp_byte(value):
    """A 0-255 byte from what a client sent, or None if it is not a number."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    if value != value:  # NaN
        return None
    return max(0, min(255, int(value)))


def _as_int(value):
    """Status registers come out of numpy, and -1 means "never read"."""
    if value is None or value < 0:
        return None
    return int(value)


def _answers_as_gripper(port, rq):
    """True if a gripper replies on `port`: one read of its status register.

    The same test pyrobotiqgripper's own auto-detection makes, without the
    subprocess per port it wraps each one in, and with a short timeout and no
    retries, so a port that is silent costs a fraction of a second.
    """
    from pymodbus.client import ModbusSerialClient
    client = ModbusSerialClient(port=port, baudrate=rq.BAUDRATE, parity="N",
                                stopbits=1, bytesize=8, timeout=0.3, retries=0)
    try:
        if not client.connect():
            return False
        reply = client.read_input_registers(address=2000, count=1, device_id=9)
        return not reply.isError() and len(reply.registers) > 0
    except Exception:
        return False
    finally:
        client.close()


def open_gripper(port=None, skip_ports=()):
    """Connect a 2F gripper and return (gripper, port). Raises if there is none.

    With no port given, every USB serial adapter not in skip_ports is tried.
    The ports other devices hold are left alone: on Windows an open port just
    refuses, but on Linux and macOS a Modbus request would actually be written
    into the tactile sensor's or the FT sensor's stream.
    """
    import pyrobotiqgripper as rq

    if port:
        gripper = rq.RobotiqGripper(com_port=port)
        return gripper, gripper.com_port
    candidates = usb_serial_ports(skip_ports)
    for candidate in candidates:
        if _answers_as_gripper(candidate, rq):
            return rq.RobotiqGripper(com_port=candidate), candidate
    raise rq.GripperConnectionError(
        "no gripper answered on " + (", ".join(candidates) or "any free USB serial port"))


def _ready(status):
    """Activated, and not stopped on a fault."""
    return (status.get("gSTA") == GSTA_ACTIVATED
            and status.get("gFLT") not in GFLT_BLOCKING)


class GripperController:
    """Owns the gripper and the one thread that talks to it.

    A Modbus round trip takes tens of milliseconds and pyrobotiqgripper keeps
    its command and status history in plain arrays, so nothing else touches the
    gripper object: websocket handlers only record what was asked for, and the
    worker sends the latest request, dropping the ones a dragged slider
    produced in between, and polls the status the rest of the time.
    """

    def __init__(self, gripper, port):
        self.gripper = gripper
        self.port = port
        self._lock = threading.Lock()
        self._pending_move = False
        self._pending_activate = False
        self.activating = False
        # What the sliders show. Position is filled in from the gripper's actual
        # position on the first status read, so the sliders start where it is.
        self.command = {"position": None, "speed": DEFAULT_SPEED, "force": DEFAULT_FORCE}
        self.status = {}
        self.error = None

    # -- requests, from the websocket handler --

    def request_move(self, position=None, speed=None, force=None):
        with self._lock:
            for key, value in (("position", position), ("speed", speed), ("force", force)):
                # Anything that is not a number is dropped, not raised: the
                # caller is the websocket handler, and an exception there
                # would disconnect the client.
                value = _clamp_byte(value)
                if value is not None:
                    self.command[key] = value
            if self.command["position"] is not None:
                self._pending_move = True

    def request_activate(self):
        with self._lock:
            self._pending_activate = True

    def snapshot(self):
        with self._lock:
            st = self.status
            return {
                "port": self.port,
                "activated": _ready(st),
                "activating": self.activating,
                "position": st.get("gPO"),     # 0 open .. 255 closed
                "requested": st.get("gPR"),
                "current": st.get("gCU"),      # roughly 10 mA per count
                "object": st.get("gOBJ"),      # 0 moving, 1/2 contact, 3 at position
                "fault": st.get("gFLT"),
                "command": dict(self.command),
                "error": self.error,
            }

    # -- the worker --

    def run(self):
        """Blocks forever; run it on a daemon thread."""
        period = 1.0 / POLL_HZ
        while True:
            with self._lock:
                activate, self._pending_activate = self._pending_activate, False
                move = self._pending_move
                self._pending_move = False
                command = dict(self.command)
            try:
                if activate:
                    self._activate()
                elif move and _ready(self.status):
                    # start=True sets rACT/rGTO in the same write, so a gripper
                    # that was activated by someone else still moves.
                    self.gripper.move(command["position"], command["speed"],
                                      command["force"], wait=False,
                                      readStatus=True, start=True)
                else:
                    self.gripper.readStatus()
                self._store_status()
                self.error = None
            except GripperFaultError as exc:
                # pyrobotiqgripper stores the status before raising, so it
                # carries the fault. The gripper then counts as not ready
                # (see _ready), and the page offers Activate, which resets it.
                self._store_status()
                self.error = f"fault: {exc}"
                time.sleep(0.5)
            except Exception as exc:
                self.error = f"{type(exc).__name__}: {exc}"
                traceback.print_exc(file=sys.stderr)
                time.sleep(0.5)  # do not hammer a gripper that has gone away
            time.sleep(period)

    def _activate(self):
        # The activation routine fully opens and closes the gripper and takes a
        # few seconds; the page says so while it runs.
        # It resets the gripper first, which together with the activation
        # clears a major fault.
        self.activating = True
        try:
            self.gripper.activate()
        finally:
            self.activating = False
        # Activation leaves the fingers wherever the routine ended; follow them.
        with self._lock:
            self.command["position"] = None

    def _store_status(self):
        raw = self.gripper.status(refreshStatus=False)
        status = {k: _as_int(raw.get(k)) for k in
                  ("gSTA", "gPO", "gPR", "gCU", "gOBJ", "gFLT")}
        with self._lock:
            self.status = status
            if self.command["position"] is None and status["gPO"] is not None:
                self.command["position"] = status["gPO"]
