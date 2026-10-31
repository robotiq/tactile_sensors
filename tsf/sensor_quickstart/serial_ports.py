"""
Which serial ports the device searches may try.

The tactile sensor, the force/torque sensor and the gripper each sit on their
own USB serial adapter, and each search has to stay off the ports the others
already hold. A port can be named several ways for one device -- on Linux the
udev rules give the tactile sensor /dev/rq_tsf85_0 while the port list shows
/dev/ttyUSB0, and /dev/serial/by-id/... works too -- so ports are compared
through port_key(), never as raw strings.
"""

import os


def port_key(port):
    """A name for the device behind `port`, the same whichever alias is used."""
    port = str(port)
    if os.name != "nt" and port.startswith("/"):
        # Symlinks resolve to the tty. Not on Windows, where realpath would
        # turn "COM4" into a file path in the working directory.
        port = os.path.realpath(port)
    return port.upper()


def skip_set(skip_ports):
    return {port_key(p) for p in skip_ports if p}


def usb_serial_ports(skip_ports=()):
    """USB serial adapters not in skip_ports, ttyUSB-style names first.

    USB adapters only: that is how every device here is cabled. Other ports are
    not just a waste of time -- on Windows each attempt to open a Bluetooth
    serial link blocks for seconds before failing, and the search looks like a
    hang. An explicit --ft-port or --gripper-port still reaches any port.
    """
    from serial.tools import list_ports
    skip = skip_set(skip_ports)
    ports = sorted((p for p in list_ports.comports()
                    if p.vid is not None and port_key(p.device) not in skip),
                   key=lambda p: (p.device.find("USB") < 0, p.device))
    return [p.device for p in ports]
