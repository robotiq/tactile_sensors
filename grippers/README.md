# Robotiq Grippers C++ SDK

A standalone, ROS-independent C++ SDK for controlling Robotiq adaptive grippers
over their Modbus RTU serial link. Cross-platform: Linux, Windows, macOS.

The 2F-85, 2F-140 and Hand-E share one Modbus register map, so the SDK speaks to
all of them and nothing in it is model-specific. Hardware validation to date is
on a 2F-85.

Developed and maintained by Robotiq; questions and bug reports go to
[Robotiq/grippers/issues](https://github.com/Robotiq/grippers/issues).

Robotiq's [ROS 2 driver](https://github.com/robotiq/ros) is built on this SDK — it
is where `robotiq_driver` gets its serial link and Modbus exchange.

## Design

A layered API around a shared process image:

- **`Gripper`** — the API for applications. Construction opens the
  link, reads the gripper status (it fails when no gripper answers), and
  starts exchanging. All Modbus traffic happens in the background
  exchange cycle (one FC 0x17 transaction per period, up to ~200 Hz at
  115200 baud). The command image is seeded from the gripper's own
  state echoes before anything is written — connecting never disturbs
  a running gripper.

`setCommand()`/`getStatus()` exchange whole `GripperCommand`/`GripperStatus`
blocks. Each block has named fields (`command.positionRequest`,
`command.speed`, ...) and small accessors for its packed action/status
byte, plus the raw bytes through `data()`. Reads stay whole-snapshot, so
consecutive fields never come from different exchange cycles. The block byte layout and status
bit masks are published in `Robotiq/gripper/register_map.hpp`, and the
Modbus register addresses in `Robotiq/detail/modbus_constants.hpp`, mirroring
the instruction manual.

The Modbus protocol layer is [nanoMODBUS](https://github.com/debevv/nanoMODBUS);
serial transport is [libserialport](https://sigrok.org/wiki/Libserialport).

## Building

Requirements: CMake ≥ 3.16, a C++17 compiler, libserialport.

| Platform | libserialport |
|----------|----------------|
| Ubuntu/Debian | `sudo apt install libserialport-dev` |
| macOS | `brew install libserialport` |
| Windows | MSYS2 — see [Windows (MSYS2)](#windows-msys2) below |

```sh
cmake -S sdk_cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build            # run unit tests, no hardware needed
```

### Windows (MSYS2)

Neither vcpkg nor Conan Center packages libserialport, so the supported
Windows toolchain is MSYS2/GCC — the same environment this repo's CI
uses. MSYS2 is a Windows distribution of Unix tooling with `pacman`
(the Arch Linux package manager) and a large repository of prebuilt
native libraries.

1. Install MSYS2 from [msys2.org](https://www.msys2.org)
   (or `winget install MSYS2.MSYS2`).
2. Open the **MSYS2 UCRT64** shell from the Start menu.
3. Install the toolchain and dependencies:

   ```sh
   pacman -Syu
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc \
             mingw-w64-ucrt-x86_64-cmake \
             mingw-w64-ucrt-x86_64-ninja \
             mingw-w64-ucrt-x86_64-libserialport
   ```

4. Build and test as usual, from the same shell:

   ```sh
   cmake -S sdk_cpp -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j
   ctest --test-dir build
   ```

This produces native Windows binaries (GCC, no emulation layer). MSVC
is not currently supported: libserialport ships no MSVC package, so a
Visual Studio build would have to compile libserialport itself.

## Getting started

A complete example — waiting for motion to settle, reading the
position back, injecting a log sink — is built as described in the Building section
and can be found here:
[`sdk_cpp/examples/move_gripper.cpp`](sdk_cpp/examples/move_gripper.cpp)

Its per-step log lines end with `toString()` of the status block: the manual's
mnemonics with decoded states and fault codes, e.g.
`gACT=1 gGTO=1 gSTA=Complete(0x3) gOBJ=AtRequestedPosition(0x3) gFLT=None(0x0)
kFLT=None(0x0) gPR=255 gPO=254 gCU=16`. `toString()` renders the command
block and the individual enums the same way.

Run it by executing:
```sh
./build/examples/move_gripper /dev/ttyUSB0    # Linux (macOS: /dev/tty.usbserial-XXXX)
./build/examples/move_gripper.exe COM3        # Windows: find the port in Device Manager
```

The example activates the gripper (calibration sweep), opens, and
closes — keep the jaws clear.

### Without a gripper

`makeFakeGripper()` returns a `Gripper` driving a fake device instead of a
serial port, for bring-up, demos and CI on machines with no hardware attached:

```cpp
#include <Robotiq/gripper/fake/gripper_factory.hpp>

auto gripper = Robotiq::makeFakeGripper();   // no port opened
```

Everything above the wire is the real thing — the typed blocks, the exchange
cycle, the process image, `activate()` / `recoverFromFault()`. The device below
it is deliberately minimal: activation completes instantly and the fingers are
wherever they were last commanded to be. There is no motion profile, no travel
time, no object detection and no fault injection.

## Consuming from CMake

```cmake
find_package(grippers REQUIRED)           # installed
# or: add_subdirectory(path/to/grippers/sdk_cpp)
target_link_libraries(your_target PRIVATE Robotiq::grippers)
```

## Serial port notes

- **Linux**: add yourself to the `dialout` group for `/dev/ttyUSB*` access.
  The SDK sets the FTDI `latency_timer` to 1 ms automatically when it has
  permission (the kernel default of 16 ms triples Modbus latency); for
  unprivileged use, ship a udev rule that sets it at plug time.
- **Windows**: the FTDI latency timer is a driver setting (Device Manager →
  COM port → Port Settings → Advanced → Latency Timer); set it to 1 ms — at its
  16 ms default the gripper's reply waits in the adapter and the exchange rate
  caps near 60 Hz however fast the loop paces. With it at 1 ms, configured
  rates are delivered exactly (measured to 200 Hz); the SDK paces with a
  high-resolution timer on Windows 10 1803+, falling back to the standard
  sleep — quantized to the ~15.6 ms OS tick — on older systems.
- **macOS**: the FTDI latency timer defaults to 16 ms — capping the exchange
  rate near ~60 Hz — and macOS offers no way to lower it from the SDK. To run
  faster, install [FTDI's VCP driver](https://ftdichip.com/drivers/vcp-drivers/)
  and set its `LatencyTimer` to `1` (in the driver's `Info.plist`); it then
  applies to every open, including this SDK's. On macOS 11+ also approve the
  driver in System Settings → Privacy & Security and make sure it — not
  Apple's built-in FTDI driver — binds your adapter (`kextstat | grep -i ftdi`).
  Otherwise ~60 Hz is the ceiling on the default driver.
- Factory-default link settings: 115200 baud, 8N1, Modbus slave 0x09.
- Port naming: `/dev/ttyUSB0` on Linux, `COM3` on Windows,
  `/dev/tty.usbserial-XXXX` on macOS.

## Embedded / bare-metal builds

The SDK core — `Gripper` and its threaded exchange loop — compiles for
freestanding targets (e.g. STM32 microcontrollers, arm-none-eabi). Everything
OS-flavored is injectable; two CMake options select what ships with it:

- **`GRIPPERS_HOSTED=OFF`** (default ON) drops the hosted conveniences: the
  `std::thread`-backed `Platform` (`makeDefaultPlatform()`), and
  the stderr default logger. Construct `Gripper` with its platform-taking
  constructor and a `Platform` implemented over your RTOS.
  `ports/threadx/threadx_platform.hpp` is the working reference (Azure RTOS
  ThreadX, with the exchange task's stack size and priority as constructor
  arguments); porting to another RTOS means implementing its four members over
  the native primitives.
- **`GRIPPERS_BUILD_DEFAULT_SERIAL=OFF`** (default follows `GRIPPERS_HOSTED`)
  drops the libserialport-backed `DefaultSerial` and its dependency. Inject
  your own `Serial` (e.g. a UART transport) via the
  `unique_ptr<Serial>` constructors of `detail::GripperModbusClient` /
  `Gripper`.
- **`detail::GripperModbusClient`** is the no-thread layer: one Modbus transaction
  per call, so a single-threaded superloop schedules the exchange itself. This is
  the simplest path for small MCUs and needs no RTOS — and no `Platform`.

Two integration caveats, detailed in `ports/threadx/threadx_platform.hpp`
because each presents as an unexplained hang: the injected `Serial::read` must
yield the CPU while awaiting bytes (interrupt/DMA + RTOS semaphore, never a
polled busy-wait), and `std::chrono::steady_clock` must be backed by a real
monotonic clock on the target.

## Versioning

[Semantic versioning](https://semver.org) from 1.0.0 on: patch releases fix
bugs, minor releases add API, and a breaking change to the documented API takes
a major release. The documented API is what this README and the public headers
describe — `Gripper`, the command/status blocks and the register map,
`ConnectionConfig`, `Serial`, `Platform`, `Logger`, the `toString()` free
functions, and `detail::GripperModbusClient` for the no-thread path. The
text `toString()` renders is for people, not parsers: its layout may change
in any release. Anything under
`Robotiq/detail/` that is not described here is internal and may change in any
release.

## License

BSD-3-Clause. Robotiq develops and maintains this SDK; parts of it started from
PickNik Robotics'
[ros2_robotiq_gripper](https://github.com/PickNikRobotics/ros2_robotiq_gripper)
driver (BSD-3-Clause), whose original copyright notices are preserved in the
affected files, with the full history preserved in git.
