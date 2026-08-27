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

Rendering is three.js, vendored under `web/vendor/` so the demo runs with no
internet — which matters on a show floor.

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
