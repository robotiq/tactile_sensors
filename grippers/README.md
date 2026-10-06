<!-- docs-site:exclude -->
# Robotiq Grippers C++ SDK

📖 Full documentation: https://robotiq.github.io/docs/drivers/2F%20hande/SDK/C++/
<!-- /docs-site:exclude -->

C++ driver with functions to control `Robotiq` Adaptive Grippers: 2F85, 2F140 and Hand-E. It allows high communication frequency.

> **Note:**
> With the default baudrate of the gripper the maximum achievable communication
> frequency is 250Hz. The communication frequency is set with the
> `ConnectionConfig::connectionFrequency` parameter, which defaults to 100Hz.

Cross-platform: Linux, Windows, macOS — and freestanding/RTOS targets such
as STM32 microcontrollers.

<!-- docs-site:exclude -->

## Documentation

- [Environment setup](docs/01-environment-setup.md)
- [Quick start](docs/02-quick-start.md)
- [How it works](docs/03-how-it-works.md)
- [Robust example walkthrough](docs/04-robust-example-walkthrough.md)
- [Embedded / bare-metal builds](docs/05-embedded-builds.md).

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

The injectable interfaces are the exception, and only for the side that
implements them: a minor release may add a member `Platform`, `Serial` or
`Logger` requires, since what they have to cover grows with every new target
and transport. Calling them keeps the major-release guarantee. If you maintain
an implementation out of tree — an RTOS `Platform`, a UART `Serial` — pin the
minor version; moving up gives a compile error naming the new member, and the
in-tree implementations show what to return.

Some 1.0.0 paths have moved: the register map to
`Robotiq/detail/register_map.hpp`, the Modbus client from
`detail/gripper_modbus_client.hpp` to `gripper/modbus_client.hpp`, and
`gripper/named_bit_array.hpp`, `gripper/throttle.hpp` and
`makeDefaultLogger()` into `Robotiq/detail/`. The old spellings still work
and warn; they go away in the next major release. To get the default
`Logger`, pass a null one.

## License

BSD-3-Clause. Robotiq develops and maintains this SDK; parts of it started from
PickNik Robotics'
[ros2_robotiq_gripper](https://github.com/PickNikRobotics/ros2_robotiq_gripper)
driver (BSD-3-Clause), whose original copyright notices are preserved in the
affected files, with the full history preserved in git.

<!-- /docs-site:exclude -->
