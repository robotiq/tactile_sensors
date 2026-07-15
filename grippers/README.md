# Robotiq Grippers C++ SDK

> ⚠️ **In development — alpha.** APIs are subject to change without notice
> until v1.0.0. If you build on this today, pin an exact commit and expect
> breakage.

A standalone, ROS-independent C++ SDK for controlling Robotiq 2F adaptive
grippers (2F-85 / 2F-140 / Hand-E class) over their Modbus RTU serial link.
Cross-platform: Linux, Windows, macOS.

**Current state: scaffolding only.** The repo builds and tests on all three
platforms, but no gripper functionality has landed yet. The
[ROS 2 driver](https://github.com/robotiq/ros) will consume this SDK; it is
equally intended for direct use — the same audience served by tools like
pyRobotiqGripper, with a supported C++ core.

## Planned design

Everything in this section is forthcoming, in roadmap order below.

Two operating modes over a layered API:

- **Synchronous block access**: on-demand status-block reads and
  command-block writes (Modbus FC 0x03/0x10), deliberately scoped to the
  two register blocks documented in the instruction manual.
- **Runtime mode**: a background loop exchanging the command and
  status register blocks in a single FC 0x17 transaction per cycle at
  maximum frequency (~200 Hz+), with thread-safe accessors.

Layer 1 exposes raw registers/bytes; higher layers (typed structs,
`getActualPosition()`-style accessors) come later.

The Modbus protocol layer is [nanoMODBUS](https://github.com/debevv/nanoMODBUS)
(vendored, the same library used inside Robotiq gripper firmware); the
serial transport will be built on
[libserialport](https://sigrok.org/wiki/Libserialport).

## Building

Requirements: CMake ≥ 3.16, a C++17 compiler, libserialport.

| Platform | libserialport |
|----------|----------------|
| Ubuntu/Debian | `sudo apt install libserialport-dev` |
| macOS | `brew install libserialport` |
| Windows | MSYS2: `pacman -S mingw-w64-ucrt-x86_64-libserialport` (build in a UCRT64 shell) |

```sh
cmake -S sdk_cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build            # unit tests, no hardware needed
```

## Consuming from CMake

```cmake
find_package(grippers REQUIRED)           # installed
# or: add_subdirectory(path/to/grippers/sdk_cpp)
target_link_libraries(your_target PRIVATE Robotiq::grippers)
```

## Roadmap

1. Configuration mode (raw registers)
2. Runtime mode: cyclic FC 0x17 loop, thread-safe byte access
3. Layer 2/3: typed command/status structs, convenience accessors
4. High-level configuration helpers, Python bindings

## License

BSD-3-Clause. Portions derived from PickNik Robotics'
[ros2_robotiq_gripper](https://github.com/PickNikRobotics/ros2_robotiq_gripper)
driver (BSD-3-Clause); original copyright notices are preserved in the
affected files and full history is preserved in git.
