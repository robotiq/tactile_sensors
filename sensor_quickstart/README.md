# Simple Tactile Sensor Check Tool

Lightweight cross-platform tool to test TSF-85 connections.

## Quick Start — Terminal

### Linux
```bash
cd sensor_quickstart
./run_quick_connect.sh
```

### Windows
```batch
cd sensor_quickstart
run_quick_connect.bat
```

That's it! The script handles everything automatically.

---

## Quick Start — Web Viewer

A browser-based dashboard: tactile heatmaps, dynamic time-series with FFT, IMU plots, and an optional 3D view of the gripper posed from the fingertip IMUs.

### Linux
```bash
cd sensor_quickstart
./run_web_viewer.sh
```

### Windows
```batch
cd sensor_quickstart
run_web_viewer.bat
```

The script sets up the environment, connects to the sensor, and opens your browser to `http://localhost:8080`. There are three tabs:

**Overview** (the default) — a column per finger: its tactile heatmap (7x4,
baseline-subtracted) with that finger's dynamic trace directly beneath it.
Finger 1 is on the left, to match the hardware rather than the index. This
layout fills whatever window it is given and never scrolls, so you can size the
window to part of the screen and keep another tool visible alongside it.

**Dynamic Sensors** — dynamic tactile time-series and spectrum, per finger.

**IMU** — accelerometer and gyroscope, X/Y/Z, per finger.

Only the tab you are looking at is computed and sent, so the other two cost
nothing. The server shuts down automatically when you close the browser tab.

### The 3D gripper

Ticking **3D gripper** in the header adds a third column to the Overview: a
2F-85 posed from the fingertip IMUs, with the force/torque wrench drawn at its
base. It is off by default and the column is simply absent until you ask for
it — the model is about 3 MB of three.js and baked meshes, and none of it is
fetched until the box is ticked. The choice is remembered per browser.

The panel says on its face which half is estimated and which is measured: the
finger tilt is inferred from the IMUs, the wrench is read from the sensor.

**Zero Force** re-zeroes the force/torque sensor. It sits under the gripper and
so carries its weight — around 9 N before anything touches the fingers — which
is subtracted at startup. That zero holds only for the orientation it was taken
in, so re-zero after turning the gripper over. The readout says "zeroing" while
the new zero is being taken.

**Refresh** sets how often the page redraws: 5 Hz by default, up to 30 Hz.
Higher looks smoother but a typical office laptop stops keeping up above about
5 Hz. It is per browser and remembered, so two machines on one viewer can run
at different rates.

### Simulation

```bash
./run_web_viewer.sh --sim
```

Synthetic data with no hardware attached: a moving pressure blob, a dynamic
tone, and fingertips sweeping through their travel. Useful for working on the
viewer itself, or for showing it on a machine with no sensor. `--sim` may go
anywhere on the command line, and the launcher skips its permission setup and
device scan for it.

`tools/simulate_sensor.py` has more knobs than the flag exposes — `--tip-sweep`,
`--tip-hold`, `--tilt`, `--force-finger`, `--peak-force`, `--no-force` — and can
be run directly.

### Options

| Option | Effect |
|---|---|
| `--port N` | HTTP port (default 8080; the WebSocket uses N+1) |
| `--sim` | synthetic data, no hardware |
| `--ft-port DEV` | force/torque serial port (default: autodetect) |
| `--no-ft` | skip the force/torque sensor entirely |
| `--static-floor N` | suppress static deflections below N counts (default 25; `0` shows every count) |

A missing or unplugged force/torque sensor is reported and stepped over, never
fatal: the pads and the gripper keep working and the readout says there is no
sensor.

The autodetect only reads from the ports it tries until one identifies itself as
a force/torque sensor. The 2F gripper answers on the same Modbus id, so nothing
is written to a device that has not said what it is. A sensor that is streaming
but not answering can therefore only be found by naming its port with
`--ft-port`, which also allows a sensor model the reader does not recognise.

---

## Where the gripper geometry comes from

Nothing is drawn by hand. `tools/build_gripper_geometry.py` reads three sources
and bakes them into `web/gripper_geometry.js` plus `web/gripper_meshes.bin`,
both committed so the viewer needs none of them at runtime:

| Source | What it provides |
|---|---|
| `robotiq_description` (ROS) | link meshes and the joint origins |
| `robotiq_2f_85_gripper_visualization` (ros-industrial) | the finger pad, defined as a box primitive rather than a mesh |
| Isaac Sim `Robotiq_2F_85_physics_compliant.usda` | the five-bar pivots, modelled as real joints rather than the ROS mimics |

Regenerate with:

```bash
python3 tools/build_gripper_geometry.py <robotiq_description> \
    --pad-description <robotiq_2f_85_gripper_visualization>/urdf/robotiq_arg2f_85_model_macro.xacro \
    --linkage <isaac assets>/Gripper_2F85/payloads/Robotiq_2F_85_physics_compliant.usda
```

All three are published under permissive licences — BSD-3-Clause for
`robotiq_description` (PickNik Robotics), BSD for ros-industrial's
`robotiq_2f_85_gripper_visualization`, and CC BY 4.0 for the Isaac Sim asset —
and the generated files carry that attribution in their headers.

Rendering is three.js (MIT), vendored under `web/vendor/` so the 3D panel needs
no internet. Plotly is still fetched from a CDN, so the page as a whole is not
yet offline-proof.

## The force/torque wrench

A wrench is a force along a line plus a twist about that same line, so it is
drawn that way rather than as two arrows sharing an origin:

```
Fhat = F / |F|
Mpar = (M . Fhat) Fhat      the twist no translation can remove
r    = (F x M) / |F|^2      offset to the line of action
```

The dashed line is the line of action. Press off-centre and it slides towards
where the load actually acts, which makes the lever arm visible as geometry
instead of as a second abstract vector. `r` grows as `1/|F|^2`, so below a few
newtons the line of action is meaningless; under that floor the arrow falls back
to the sensor origin carrying the whole moment as a twist.

The sensor origin is **approximate**: the FT sensor mounts between the flange
and the coupling, and while the adapter is 11 mm the sensor's own stack height
is not documented in any repo to hand. It only shifts where the wrench is
anchored.

## What the fingertip IMUs can and cannot see

Each finger is a closed five-bar with two degrees of freedom: one driven, one
free. The free one is the compliance that lets the distal phalanx wrap an
object, and it is the one the fingertip IMU can see. The drive is assumed
**fully open**, which grounds the outer knuckle and leaves a four-bar whose
coupler is the distal phalanx; measuring that link's angle closes the mechanism
and the rest follows in closed form.

The opening itself cannot be recovered from the IMUs: a fingertip measures the
*sum* of the joint angles along its chain, which in parallel mode is identically
zero at every opening. Showing a real opening needs the gripper's own position
feedback.

The angle is referenced to the pose the gripper was in when the server started —
upright, still and fully open is assumed — and the first 200 samples fix that
zero. In any other orientation the angles are plausible but meaningless, which
the readout flags rather than hides.

---

## Requirements

- **Python 3.7+**: [Download Python](https://www.python.org/downloads/)
  - ✅ Check "Add Python to PATH" during installation
  - ✅ After installing, restart your terminal/command prompt
- **pyserial**: Installed automatically by the script

---

## What It Does

1. Checks for Python installation
2. Creates virtual environment (`.venvSimpleCheck`)
3. Installs dependencies
4. Detects sensor
5. Displays real-time sensor data

---

## Expected Output

```
================================================================================
                        Robotiq Tactile Sensor Monitor
================================================================================
Data Rate: 160.234 KB/s  |  Refresh Rate: 1000.1 Hz  |  Total Packets: 15234

FINGER 0
--------------------------------------------------------------------------------
  Static Tactile (7 rows × 4 columns):
      0     1     2     3
      4     5     6     7
      8     9    10    11
     12    13    14    15
     16    17    18    19
     20    21    22    23
     24    25    26    27

  Dynamic Tactile:    123

  Accelerometer: X=    12  Y=   -45  Z=  1024
  Gyroscope:     X=     3  Y=    -2  Z=     1
  Magnetometer:  X=   -15  Y=    23  Z=   -87

  Open Byte:    0

FINGER 1
--------------------------------------------------------------------------------
  [... same format ...]

================================================================================
Press Ctrl+C to exit
```

---

## Troubleshooting

### Sensor Not Found

**Linux:**
- Check USB connection
- Verify sensor is plugged in
- Try different USB port

**Windows:**
- Check Device Manager (Win+X → Device Manager → Ports)
- Sensor appears as "USB Serial Device" or "Cypress USB UART"
- Try different USB port
- **VM users**: USB passthrough may not work reliably for serial devices

### No Data Displayed

- Unplug and replug the sensor
- Close terminal and rerun script
- Sensor may need to be reset

### Python Not Found (Windows)

- Install Python from [python.org](https://www.python.org/downloads/)
- Must check "Add Python to PATH" during installation
- Restart command prompt after installing

---

## Learn More

- **Python**: [python.org](https://www.python.org/)
- **Virtual Environments**: [Python venv documentation](https://docs.python.org/3/library/venv.html)
- **pyserial**: [pyserial documentation](https://pyserial.readthedocs.io/)

---

## Sensor Details

- **Baud rate**: 115200
- **Format**: 8N1 (8 data bits, no parity, 1 stop bit)
- **USB VID:PID**: `16d0:14cc` (Robotiq) or `04b4:f232` (Cypress, older units)
- **Data**: 28 tactile sensors per finger (7×4 grid) + IMU + dynamic sensor

---

## File Structure

```
sensor_quickstart/
├── quick_connect.py         # Terminal-based sensor monitor
├── web_viewer.py            # Web-based visualization server
├── protocol.py              # USB protocol implementation
├── ft_source.py             # Force/torque source interface
├── ft_modbus.py             # Force/torque reader (Modbus RTU)
├── requirements.txt         # Dependencies (pyserial, websockets)
├── run_quick_connect.sh     # Linux launcher (terminal)
├── run_quick_connect.bat    # Windows launcher (terminal)
├── run_web_viewer.sh        # Linux launcher (web UI)
├── run_web_viewer.bat       # Windows launcher (web UI)
├── tools/                   # Simulator, sensor probes, geometry generator
│   ├── simulate_sensor.py   #   --sim runs the viewer on synthetic data
│   ├── imu_axes.py          #   which IMU axis is which, on hardware
│   ├── ft_probe.py          #   force/torque sensor identification
│   ├── tip_mirror_check.py  #   fingertip angle: the two fingers agree
│   ├── tip_limit_check.py   #   fingertip angle: stays inside joint travel
│   └── build_gripper_geometry.py
├── web/                     # Web UI assets
│   ├── index.html
│   ├── app.js               #   tabs, charts, websocket
│   ├── gripper3d.js         #   the 3D panel, imported only when enabled
│   ├── gripper_geometry.js  #   generated; meshes + five-bar pivots
│   ├── gripper_meshes.bin   #   generated; mesh vertex data
│   ├── vendor/              #   three.js (MIT), vendored
│   └── style.css
└── README.md
```

---

**Press Ctrl+C to stop the sensor monitor**
