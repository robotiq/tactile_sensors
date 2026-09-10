<!-- docs-site:exclude -->
# Robotiq Grippers C++ SDK

📖 Full documentation: https://robotiq.github.io/docs/drivers/2F%20hande/SDK/C++/
<!-- /docs-site:exclude -->

The 2F-85, 2F-140 and Hand-E share one Modbus register map, so `Gripper` and
the register blocks speak to all of them unchanged; the model enters only in
the SI unit scaling, through a `DeviceProfile`. Hardware validation to date is
on a 2F-85.
Cross-platform: Linux, Windows, macOS — and freestanding/RTOS targets such
as STM32 microcontrollers.

<!-- docs-site:exclude -->

## Documentation

- [Environment setup](docs/01-environment-setup.md)
- [Quick start](docs/02-quick-start.md)
- [How it works](docs/03-how-it-works.md)
- [Robust example walkthrough](docs/04-robust-example-walkthrough.md)
- [Embedded / bare-metal builds](docs/05-embedded-builds.md).

<!-- /docs-site:exclude -->

## Feature requests and bug reports
Submit here:
[Robotiq/grippers/issues](https://github.com/Robotiq/grippers/issues)

## Versioning

[Semantic versioning](https://semver.org) from 1.0.0 on: patch releases fix
bugs, minor releases add API, and a breaking change to the documented API takes
a major release. The documented API is what this README and the headers under
`Robotiq/gripper/` describe — `Gripper`, the command/status blocks,
`ConnectionConfig`, `Serial`, `Platform`, `Logger`, the `toString()` free
functions, `DeviceProfile` and the SI unit conversions, and
`GripperModbusClient` for the no-thread path. The text `toString()` renders is
for people, not parsers: its layout may change in any release. Everything
under `Robotiq/detail/` is internal and may change in any release.

Two paths this section named at 1.0.0 have moved: the register map to
`Robotiq/detail/register_map.hpp`, and the Modbus client from
`detail/gripper_modbus_client.hpp` to `gripper/modbus_client.hpp`. Both old
paths still exist as shims that include the new header and warn; they go away
in the next major release. Headers this section never named —
`gripper/named_bit_array.hpp` and `gripper/throttle.hpp` — moved into
`Robotiq/detail/` without a shim, as did `makeDefaultLogger()`; pass a null
`Logger` to get the build's default one.

## License

BSD-3-Clause. Robotiq develops and maintains this SDK; parts of it started from
PickNik Robotics'
[ros2_robotiq_gripper](https://github.com/PickNikRobotics/ros2_robotiq_gripper)
driver (BSD-3-Clause), whose original copyright notices are preserved in the
affected files, with the full history preserved in git.
