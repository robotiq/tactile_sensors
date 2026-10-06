"""
The force/torque data source the viewer reads from.

Only the interface lives here, so anything can play the part: `ft_modbus.py`
reads a real sensor over Modbus RTU, and `tools/simulate_sensor.py` synthesises
one. Pass any object implementing `read` to `run_web_viewer(ft_source=...)`.
"""


class FTSource:
    """A source of force/torque readings.

    `read` is run on a daemon thread and is expected to block forever, calling
    `callback(timestamp, (fx, fy, fz, mx, my, mz))` for every sample — forces in
    newtons, moments in newton-metres, expressed at the sensor's own origin.
    """

    def read(self, callback):
        raise NotImplementedError
