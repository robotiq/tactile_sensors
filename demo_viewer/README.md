# Trade-show demo viewer — tactile + gripper + force/torque

A single page showing what the sensors on a 2F-85 can tell you at once:

- **Static pressure** — the 7x4 tactile heatmap per finger, baseline-subtracted
- **Dynamic** — both fingers' dynamic tactile traces on one plot
- **Finger proprioception** — a 3D 2F-85 whose five-bar linkage is posed from the
  fingertip IMUs, with the force/torque wrench drawn at its base

This is a demo, not the general-purpose tool. For plain sensor bring-up — raw
IMU traces, FFT, per-finger plots — use `../sensor_quickstart`, which stays
deliberately boring.

## Run

```bash
./run_demo.sh          # real sensor
./run_demo.sh --sim    # synthetic data, no hardware attached
```

Both open a browser on the viewer. The server shuts down when the last tab
closes.

## Where the geometry comes from

Nothing here is drawn by hand. `tools/build_gripper_geometry.py` reads three
sources and bakes them into `web/gripper_geometry.js` plus
`web/gripper_meshes.bin`, both committed so the viewer needs none of them at
runtime:

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

Rendering is three.js (MIT), vendored under `web/vendor/` so the demo runs with
no internet, which is the normal state of a show floor. Plotly is still fetched
from a CDN, so the charts are not yet offline-proof.

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
instead of as a second abstract vector — press on a fingertip and the line runs
through that fingertip. The arrow is slid along that line to finger height so it
is drawn against what it is pushing; the teal arc is `Mpar`.

`r` grows as `1/|F|^2`, so below a few newtons the line of action is meaningless
and jittery. Under that floor the arrow falls back to the sensor origin carrying
the whole moment as a twist, and the offset is clamped so it can never leave the
frame.

Two caveats. The sensor origin is **approximate**: the FT sensor mounts between
the flange and the coupling, and while the adapter is 11 mm the sensor's own
stack height is not documented in any repo to hand — it only shifts where the
wrench is anchored. And a missing or unplugged FT sensor is reported, never
fatal: the gripper keeps posing and the readout says there is no sensor.

Simulate a press with `./run_demo.sh --sim`, which applies a force at a
fingertip and derives the wrench from it; `--force-finger 1`, `--peak-force`,
and `--no-force` vary or remove it.

`ft_modbus.py` reads a real sensor over Modbus RTU. It is deliberately minimal —
enough to identify the sensor, start and stop the compensated force/torque
stream, and decode its frames, rather than the full register map. The sensor can
also stream its uncompensated values and its raw pad readings; neither is useful
here, so those modes are not implemented. It probes 19200 and
115200 baud and uses whichever replies, and never writes to the sensor's
configuration beyond starting and stopping the stream.

`ft_source.py` holds just the interface, so the simulator and the real reader
are interchangeable. A missing or unplugged sensor is reported and stepped over:
the gripper keeps posing and the readout says there is no sensor.

## The mechanism, and what the IMUs actually see

Each finger is a closed five-bar with two degrees of freedom: one driven, one
free. The free one is the compliance that lets the distal phalanx wrap an
object, and it is the one the fingertip IMU can see.

The drive is assumed **fully open**, which grounds the outer knuckle and leaves
a four-bar whose coupler is the distal phalanx. Measuring that link's angle
closes the mechanism, so the rest follows in closed form and the whole linkage
articulates.

The opening itself cannot be recovered from the IMUs: a fingertip measures the
*sum* of the joint angles along its chain, which in parallel mode is identically
zero at every opening. Showing a real opening needs the gripper's own position
feedback.
