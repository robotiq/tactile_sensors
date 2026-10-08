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
The options for the pads sit in the bar under them. **Reset Baseline** takes
the current reading as the new zero, and **Unzeroed** shows the values without
the baseline subtracted. The **Pressure** list picks how the pads are drawn:
**Per taxel** shows one cell per taxel, exactly as measured; **Interpolated**
draws half-pitch squares: one on each taxel centre with its own value, one between each two neighbouring
taxels with their mean, and one between each four with the mean of the four.
Nothing is extrapolated: the edge squares stretch to the border with the edge
taxels' values. The taxel boundaries stay drawn on top. The choice is
remembered by the browser. Finger 1 is on the left, to match the hardware rather than the index. This
layout fills whatever window it is given and never scrolls, so you can size the
window to part of the screen and keep another tool visible alongside it.

**Dynamic Sensors** — dynamic tactile time-series and spectrum, per finger.

**IMU** — accelerometer and gyroscope, X/Y/Z, per finger.

Only the tab you are looking at is computed and sent, so the other two cost
nothing. The server shuts down automatically when you close the browser tab.

### The 3D gripper

The **3D gripper** box in the header shows a third column on the Overview: a
2F-85 posed from the fingertip IMUs, with the force/torque wrench drawn at its
base. It is on by default. Unticking it removes the column, and the choice is
remembered per browser. The model is about 3 MB of three.js and baked meshes,
fetched on first load (negligible from localhost); a browser that has the box
unticked never fetches it.

The panel says on its face which half is estimated and which is measured: the
finger tilt is inferred from the IMUs, the wrench is read from the sensor.
With a gripper connected (see below) the drawing also follows its opening,
from the gripper's position feedback; without one it is held fully open.

The finger tilt is measured against gravity, so keep the gripper's orientation
fixed while the viewer runs. Which way the fingers point, up or down, is
detected from the same IMU reading and shown under the readouts. With the
gripper on its side the tilt cannot be measured: the angles are marked invalid
and a red warning is shown, rather than drawing a wrong pose.

**Zero Force** re-zeroes the force/torque sensor. It sits under the gripper and
so carries its weight — around 9 N before anything touches the fingers — which
is subtracted at startup. That zero holds only for the orientation it was taken
in, so re-zero after turning the gripper over. The readout says "zeroing" while
the new zero is being taken. The button sits at the bottom of the 3D view and
is shown only while the force/torque sensor is connected.

**Refresh** sets how often the page redraws: 5 Hz by default, up to 30 Hz.
Higher looks smoother but a typical office laptop stops keeping up above about
5 Hz, so any rate above 5 Hz shows a warning beside the list. It is per
browser and remembered, so two machines on one viewer can run
at different rates.

### Gripper control

When a 2F gripper is connected over its USB/RS485 adapter, the Overview gains a
**Gripper control** panel with three sliders: **Position** (0% fully open,
100% fully closed), **Speed** and **Force**. Moving a slider sends the move
straight away; **Open** and **Close**, beside the sliders, send the position
to either end. The panel also shows the gripper's actual position, and an
object-detection lamp that lights green when the fingers stop on something. Control goes through
[pyrobotiqgripper](https://pypi.org/project/pyrobotiqgripper/), installed by
the launcher. It needs Python 3.10 or later; on an older Python it is not
installed, and the viewer runs without the gripper controls.

The gripper is found automatically: each USB serial adapter that the tactile
and force/torque sensors are not already using is asked for the gripper's
status. To skip the
search, or if it picks the wrong port, name the port:

```bash
./run_web_viewer.sh --gripper-port /dev/ttyUSB1
run_web_viewer.bat --gripper-port COM7
```

A gripper that is not yet activated, or has stopped on a fault, shows an
**Activate** button; on a fault it resets the gripper first. Activation fully
opens and closes the fingers, so keep the space between them clear. Like
the force/torque sensor, a missing gripper is reported and stepped over: the
panel is just not shown.

### Simulation

```bash
./run_web_viewer.sh --sim
```

Synthetic data with no hardware attached: a moving pressure blob, a dynamic
tone, fingertips sweeping through their travel, and a simulated gripper for
the control panel. Useful for working on the
viewer itself, or for showing it on a machine with no sensor. `--sim` may go
anywhere on the command line, and the launcher skips its permission setup and
device scan for it.

`tools/simulate_sensor.py` has more knobs than the flag exposes — `--tip-sweep`,
`--tip-hold`, `--tilt`, `--force-finger`, `--peak-force`, `--no-force`,
`--no-gripper`, `--upside-down` — and can
be run directly.

### Options

| Option | Effect |
|---|---|
| `--port N` | HTTP port (default 8080; the WebSocket uses N+1) |
| `--sim` | synthetic data, no hardware |
| `--ft-port DEV` | force/torque serial port (default: autodetect among USB serial adapters) |
| `--no-ft` | skip the force/torque sensor entirely |
| `--gripper-port DEV` | gripper serial port (default: autodetect among USB serial adapters; with `--sim`, drives that real gripper) |
| `--no-gripper` | do not look for a gripper; no control panel |
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

Nothing needs to be installed by hand: the launch scripts set up everything
the first time they run.

- **Python 3.8+** (3.10+ for the gripper controls). If it is missing, the
  script installs Python 3.12:
  - **Windows**: for the current user only, no admin rights needed, through
    `winget`, or the python.org installer when `winget` is not available
  - **macOS**: the python.org installer, which asks for your password
  - **Linux**: the distribution's package manager (`apt`, `dnf`, `pacman` or
    `zypper`), which asks for your password
- **Python packages** (pyserial, websockets, pyrobotiqgripper): installed into
  a private virtual environment, `.venvSimpleCheck`, so nothing else on the
  computer is affected
- **Internet access** on the first run only, to download the above. After
  that everything runs offline: the web viewer's libraries are part of this
  repository, and the scripts only reinstall packages when `requirements.txt`
  changes.

---

## What It Does

1. Checks for Python, and installs it if missing
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
├── gripper_control.py       # Gripper control (pyrobotiqgripper)
├── serial_ports.py          # USB serial port search shared by the devices
├── requirements.txt         # Dependencies (pyserial, websockets, pyrobotiqgripper)
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
