"""
Reading a Robotiq force/torque sensor over Modbus RTU.

Deliberately minimal: only what this demo needs to stream compensated
force/torque. Enough of the protocol to start and stop the stream, decode the
frames, and confirm something is answering — not the full register map.

  - Modbus RTU, 8N1, at 19200 or 115200 baud, whichever the sensor answers on
  - writing to the stream-control register puts the sensor into stream mode
  - streamed frames carry six int16 values behind a two-byte header and close
    with a Modbus CRC-16
  - forces arrive in hundredths of a newton, moments in thousandths of a
    newton-metre

The sensor's own configuration is never written to beyond starting and stopping
the stream: the port is probed at both baud rates and whichever answers is used.
"""

import time

from ft_source import FTSource

SLAVE_ID = 9

REG_STREAM_CONTROL = 410      # start/stop streaming
REG_FORCE_TORQUE = 180        # compensated force/torque, six int16 registers
REG_SENSOR_TYPE = 499         # which model is on the end of the cable
FT_REGISTER_COUNT = 6

SENSOR_TYPES = {1: "FT-150", 2: "FT-300", 3: "FT-300+"}

STREAM_COMPENSATED = 0
STREAM_OFF = 3
STREAM_FRAME_BYTES = 16       # 2 header + 12 payload + 2 CRC
STREAM_HEADER = b"\x20\x4e"

FORCE_DIVISOR = 100.0         # int16 -> N
MOMENT_DIVISOR = 1000.0       # int16 -> Nm

BAUD_RATES = (19200, 115200)


class ModbusError(Exception):
    pass


def crc16(data):
    """Modbus CRC-16 (polynomial 0xA001), returned as (lsb, msb) bytes."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return bytes((crc & 0xFF, (crc >> 8) & 0xFF))


def _framed(payload):
    return payload + crc16(payload)


def read_registers_frame(address, count, slave=SLAVE_ID):
    return _framed(bytes((slave, 0x03,
                          (address >> 8) & 0xFF, address & 0xFF,
                          (count >> 8) & 0xFF, count & 0xFF)))


def write_register_frame(address, value, slave=SLAVE_ID):
    return _framed(bytes((slave, 0x10,
                          (address >> 8) & 0xFF, address & 0xFF,
                          0x00, 0x01, 0x02,
                          (value >> 8) & 0xFF, value & 0xFF)))


def _to_int16(low, high):
    value = low | (high << 8)
    return value - 0x10000 if value & 0x8000 else value


def decode_stream_frame(frame):
    """Decode one stream frame into (fx, fy, fz, mx, my, mz), N and Nm."""
    payload = frame[2:-2]
    values = [_to_int16(payload[2 * i], payload[2 * i + 1]) for i in range(6)]
    return tuple([v / FORCE_DIVISOR for v in values[:3]]
                 + [v / MOMENT_DIVISOR for v in values[3:]])


class ModbusRTUStreamSource(FTSource):
    """Streams compensated force/torque from a Robotiq FT sensor."""

    def __init__(self, port=None, baudrate=None, timeout=0.5, skip_ports=()):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        # Ports already in use by something else — the tactile sensor holds one,
        # and probing it just yields a permission error on Windows.
        self.skip_ports = {str(p).upper() for p in skip_ports if p}
        self._serial = None

    # -- connection --

    def connect(self):
        """Open the sensor and return (port, baudrate, description)."""
        import serial  # pyserial

        ports = [self.port] if self.port else self._candidate_ports()
        ports = [p for p in ports if p.upper() not in self.skip_ports]
        if not ports:
            raise ModbusError("no serial ports left to try")
        bauds = [self.baudrate] if self.baudrate else list(BAUD_RATES)

        attempts = []
        for port in ports:
            for baud in bauds:
                try:
                    handle = serial.Serial(port, baud, bytesize=serial.EIGHTBITS,
                                           parity=serial.PARITY_NONE,
                                           stopbits=serial.STOPBITS_ONE,
                                           timeout=self.timeout)
                except (OSError, serial.SerialException) as exc:
                    attempts.append(f"{port}@{baud}: {exc}")
                    continue

                self._serial = handle
                # The sensor may still be streaming from a previous run, in
                # which case it answers nothing until taken out of stream mode.
                for attempt in range(2):
                    try:
                        self.stop_stream()
                        model = self.read_sensor_type()
                        self.port, self.baudrate = port, baud
                        return port, baud, model
                    except ModbusError as exc:
                        if attempt:
                            attempts.append(f"{port}@{baud}: {exc}")
                handle.close()
                self._serial = None

        raise ModbusError("no force/torque sensor found. Tried:\n  "
                          + "\n  ".join(attempts))

    @staticmethod
    def _candidate_ports():
        from serial.tools import list_ports
        # USB serial adapters first: that is how the sensor is cabled.
        ports = sorted(list_ports.comports(),
                       key=lambda p: (p.device.find("USB") < 0, p.device))
        return [p.device for p in ports]

    def close(self):
        if self._serial is not None:
            try:
                self.stop_stream()
            except (ModbusError, OSError):
                pass
            self._serial.close()
            self._serial = None

    # -- requests --

    def _request(self, frame, expected_length):
        """Send a request and return the matching response.

        The reply can arrive buried in stream frames if the sensor has not
        stopped streaming yet, so this scans for a response that matches the
        request and passes CRC rather than assuming it starts at the first byte.
        """
        self._serial.reset_input_buffer()
        self._serial.write(frame)

        buffer = bytearray()
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            chunk = self._serial.read(max(1, self._serial.in_waiting))
            if chunk:
                buffer += chunk
            for i in range(0, len(buffer) - expected_length + 1):
                if buffer[i] != frame[0] or buffer[i + 1] != frame[1]:
                    continue
                candidate = bytes(buffer[i:i + expected_length])
                if crc16(candidate[:-2]) == candidate[-2:]:
                    return candidate

        raise ModbusError("no response" if not buffer
                          else f"no valid response in {len(buffer)} bytes")

    def read_registers(self, address, count):
        # slave + function + byte count + payload + CRC
        response = self._request(read_registers_frame(address, count), 5 + 2 * count)
        payload = response[3:3 + 2 * count]
        return [(payload[2 * i] << 8) | payload[2 * i + 1] for i in range(count)]

    def read_sensor_type(self):
        """Which model is answering, for the startup line."""
        raw = self.read_registers(REG_SENSOR_TYPE, 1)[0]
        return SENSOR_TYPES.get(raw & 0xFF, "unrecognised force/torque sensor")

    def read_force_torque(self):
        """One polled read of the compensated force/torque, in N and Nm."""
        registers = self.read_registers(REG_FORCE_TORQUE, FT_REGISTER_COUNT)
        signed = [r - 0x10000 if r & 0x8000 else r for r in registers]
        return tuple([v / FORCE_DIVISOR for v in signed[:3]]
                     + [v / MOMENT_DIVISOR for v in signed[3:]])

    # -- streaming --

    def start_stream(self):
        self._request(write_register_frame(REG_STREAM_CONTROL,
                                           (2 << 8) | STREAM_COMPENSATED), 8)

    def stop_stream(self):
        """Take the sensor out of stream mode.

        Sent without waiting for a reply: mid-stream the reply is
        indistinguishable from data until the stream actually stops. The caller
        confirms the sensor is listening again by reading a register.
        """
        self._serial.write(write_register_frame(REG_STREAM_CONTROL,
                                                (2 << 8) | STREAM_OFF))
        self._serial.flush()
        time.sleep(0.1)          # let the last in-flight frames drain
        self._serial.reset_input_buffer()

    def read(self, callback):
        if self._serial is None:
            self.connect()
        self.start_stream()

        buffer = bytearray()
        while True:
            chunk = self._serial.read(max(1, self._serial.in_waiting))
            if not chunk:
                continue
            buffer += chunk
            # Consume whole frames, resyncing on the header and the CRC.
            while len(buffer) >= STREAM_FRAME_BYTES:
                if buffer[0] != STREAM_HEADER[0]:
                    del buffer[0]
                    continue
                if buffer[1] != STREAM_HEADER[1]:
                    del buffer[:2]
                    continue
                frame = bytes(buffer[:STREAM_FRAME_BYTES])
                if crc16(frame[:-2]) != frame[-2:]:
                    del buffer[:2]
                    continue
                callback(time.monotonic(), decode_stream_frame(frame))
                del buffer[:STREAM_FRAME_BYTES]
