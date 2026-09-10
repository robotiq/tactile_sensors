"""
Find out what is on a serial port, when the force/torque autodetect says no.

The usual failure is not silence but noise: the port answers with bytes that are
not a Modbus reply, and the scan can only report "no valid response in N bytes".
This says what those bytes actually are.

    python3 tools/ft_probe.py            list the ports and what is on them
    python3 tools/ft_probe.py COM4       look closely at one port
"""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import serial  # noqa: E402
from serial.tools import list_ports  # noqa: E402

from ft_modbus import (crc16, read_registers_frame, write_register_frame,  # noqa: E402
                       REG_SENSOR_TYPE, REG_STREAM_CONTROL, STREAM_HEADER,
                       STREAM_FRAME_BYTES, STREAM_OFF, BAUD_RATES, SLAVE_ID,
                       SENSOR_TYPES)

LISTEN_SECONDS = 0.6


def show_ports():
    print("Serial ports on this machine:\n")
    for p in sorted(list_ports.comports(), key=lambda p: p.device):
        vid = f"{p.vid:04X}" if p.vid is not None else "----"
        pid = f"{p.pid:04X}" if p.pid is not None else "----"
        print(f"  {p.device:8s} {p.description}")
        print(f"           VID:PID = {vid}:{pid}   {p.manufacturer or ''}")
    print("\nA force/torque sensor is normally a plain USB-RS485 adapter.")
    print("If a port shares its VID:PID with the tactile sensor, it belongs to")
    print("that device and is not the one you are looking for.\n")


def describe(data):
    """Say what a blob of bytes looks like."""
    if not data:
        return "nothing at all"
    headers = [i for i in range(len(data) - 1)
               if data[i] == STREAM_HEADER[0] and data[i + 1] == STREAM_HEADER[1]]
    if len(headers) >= 3:
        gaps = [b - a for a, b in zip(headers, headers[1:])]
        common = max(set(gaps), key=gaps.count)
        good = sum(1 for i in headers
                   if i + STREAM_FRAME_BYTES <= len(data)
                   and crc16(data[i:i + STREAM_FRAME_BYTES - 2])
                   == data[i + STREAM_FRAME_BYTES - 2:i + STREAM_FRAME_BYTES])
        verdict = (f"force/torque stream frames: {len(headers)} headers, "
                   f"every {common} bytes, {good} passing CRC")
        if common == STREAM_FRAME_BYTES and good:
            verdict += "\n      -> this IS the force/torque sensor, already streaming"
        return verdict
    if any(b == SLAVE_ID for b in data[:4]):
        return "starts with the sensor's slave id — could be a Modbus reply"
    printable = sum(1 for b in data if 32 <= b < 127)
    if printable > len(data) * 0.7:
        return f"mostly printable text: {data[:40]!r}"
    return "not force/torque stream frames — some other device's data"


def probe(port):
    for baud in BAUD_RATES:
        print(f"\n--- {port} at {baud} baud")
        try:
            handle = serial.Serial(port, baud, timeout=0.2)
        except Exception as exc:
            print(f"    cannot open: {exc}")
            continue
        with handle:
            # 1. what arrives unprompted
            handle.reset_input_buffer()
            time.sleep(LISTEN_SECONDS)
            unprompted = handle.read(handle.in_waiting or 1)
            print(f"    unprompted: {len(unprompted)} bytes in {LISTEN_SECONDS}s")
            if unprompted:
                print(f"    first bytes: {unprompted[:24].hex(' ')}")
                print(f"    looks like: {describe(unprompted)}")

            # 2. does telling it to stop streaming quieten it?
            handle.write(write_register_frame(REG_STREAM_CONTROL, (2 << 8) | STREAM_OFF))
            handle.flush()
            time.sleep(0.3)
            handle.reset_input_buffer()
            time.sleep(LISTEN_SECONDS)
            after = handle.read(handle.in_waiting or 1)
            print(f"    after a stop-stream command: {len(after)} bytes")
            if unprompted and not after:
                print("      -> it went quiet, so it was listening: this is the sensor")

            # 3. does it answer a request, and which model is it?
            handle.reset_input_buffer()
            handle.write(read_registers_frame(REG_SENSOR_TYPE, 1))
            time.sleep(0.3)
            reply = handle.read(handle.in_waiting or 1)
            if not reply:
                print("    no reply to a register read")
            else:
                ok = len(reply) >= 7 and crc16(reply[:7 - 2]) == reply[5:7]
                print(f"    replied to a register read: {reply[:16].hex(' ')}"
                      f"  {'valid CRC' if ok else 'not a valid reply'}")
                if ok:
                    code = reply[4]
                    print(f"      -> register {REG_SENSOR_TYPE} = {code}: "
                          f"{SENSOR_TYPES.get(code, 'unrecognised model')}")

            if after and not reply:
                print("\n    It is streaming and not listening. A sensor left"
                      " streaming by an\n    earlier program answers nothing,"
                      " so its model cannot be read. Power\n    cycle it and"
                      " run this again straight away, before anything restarts"
                      "\n    the stream.")


def main():
    if len(sys.argv) < 2:
        show_ports()
        print("Run again with a port name to look at one closely.")
        return 0
    probe(sys.argv[1])
    return 0


if __name__ == "__main__":
    sys.exit(main())
