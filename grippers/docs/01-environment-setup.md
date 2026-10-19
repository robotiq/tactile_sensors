# Environment setup
This page covers setting up a project that uses the Robotiq gripper C++
SDK as a dependency: installing prerequisites, bringing the SDK into your
own build.

CMake ≥ 3.16, Git, and a C++17 compiler (GCC, Clang or MSVC; on Linux, GCC 9 or Clang 9 or newer). The SDK
pulls in no third-party library.

[![Environment setup walkthrough](https://img.youtube.com/vi/J4jhFiG1VNE/0.jpg)](https://youtu.be/J4jhFiG1VNE)

## Bring the SDK into your project
Add the **grippers** complete repository (or just `sdk_cpp/`) as a git submodule inside your own project.

Navigate to the project folder:

```bash
cd /path/to/your/main-project

git init
```

Add the sdk as a submodule of your project using git:

```bash
git submodule add https://github.com/robotiq/grippers third_party/grippers
```

This creates a folder named "grippers" inside the "third_party" folder of your project.

Check out the latest release (Linux) :

```bash
# 1. Navigate into the submodule
cd third_party/grippers

# 2. Fetch all the latest release tags
git fetch --tags

# 3. Find the most recent tag and save it to a variable
LATEST_TAG=$(git describe --tags $(git rev-list --tags --max-count=1))

# 4. Check out that specific version
git checkout $LATEST_TAG
```

Check out the latest release (Windows) :

```PowerShell
cd third_party\grippers
git fetch --tags
$LatestTag = git tag --sort=-v:refname | Select-Object -First 1
git checkout $LatestTag
```

> **Note:**
>
> You can later on update the checkout version.
>
> ```sh
> cd third_party/grippers
> git fetch
> git checkout <new-tag-or-commit>
> cd ../..
> git add third_party/grippers
> ```

The SDK's own `CMakeLists.txt` also exposes a few build-configuration
options; the defaults are right for a normal desktop build — see
[CMake options](#cmake-options) below if you need something different.

## Compilation environment

The environment to compile the C++ code of the driver differs depending on your operating system.

| Platform | Build environment |
|----------|----------------|
| Ubuntu/Debian | native terminal |
| macOS | native terminal |
| Windows | native terminal — see [Windows](#windows) below |

### Linux and macOS

Nothing to install beyond a compiler and CMake. The serial port is
driven through termios, which is part of the C library.

### Windows

The SDK talks to the COM port through the Windows API directly, so all
you need is Microsoft's C++ toolchain, which CMake finds without being
told where it is.

1. Install the compiler. The command-line **Build Tools** are all this
   SDK needs, and by far the lighter of the two ways to get them:

   ```powershell
   winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   ```

   Paste that as a single line. The Visual Studio Installer opens and
   installs Visual Studio Build Tools 2022 by itself, with nothing to
   click; `--wait` holds the prompt until that finishes, so winget
   reports success once the compiler is really there and not when the
   small bootstrapper launched. Expect a multi-gigabyte download.

   Microsoft asks
   [2.3 GB and up](https://learn.microsoft.com/en-us/visualstudio/releases/2022/system-requirements)
   for the Build Tools, against 20 to 50 GB for a typical full
   [Visual Studio](https://visualstudio.microsoft.com/downloads/)
   install. Reach for full Visual Studio only if you want the IDE for
   other work: its **Desktop development with C++** workload carries
   the same compiler, and everything below reads the same either way.
2. Install CMake and Git:

   ```powershell
   winget install Kitware.CMake
   winget install Git.Git
   ```

   Full Visual Studio already ships CMake, so skip that line if you
   took the first option above. Git you need either way.
3. Open a **new** PowerShell or Command Prompt window before building.

   The installers put their tools on the system PATH, but a window
   that was already open keeps the environment it started with — which
   is why a first build otherwise stops at `cmake : The term 'cmake'
   is not recognized`. An ordinary window is all you need; there is no
   developer shell to hunt for.

## Compile from the terminal

Add a `CMakeLists.txt` to your project's root. Here below is an example:

```cmake
# 1. Define the minimum version of CMake required to build this project
cmake_minimum_required(VERSION 3.16)

# 2. Name your project and specify it uses C++
project(RobotiqGripperApp LANGUAGES CXX)

# 3. Force C++17 standard (standard practice for modern SDKs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. Include the third-party gripper library directory
add_subdirectory(third_party/grippers/sdk_cpp)

# 5. Tell CMake to create your executable program ("quick_start") from main.cpp
add_executable(quick_start main.cpp)

# 6. Link the gripper library to your executable
target_link_libraries(quick_start PRIVATE Robotiq::grippers)
```

This assumes a `main.cpp` file (your application's entry point) also exists
at your project's root — create one if you don't already have it.

CMake can then be called from your project's root:

```sh
# 1. Configure the build system
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 2. Compile/build the project using all available CPU cores
cmake --build build -j
```

**Windows**: CMake defaults there to the Visual Studio generator, which
holds every configuration in one build directory. It ignores
`CMAKE_BUILD_TYPE`, so pick the configuration when you build instead:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Your application will then be compiled and you will be able to launch it
passing whatever arguments your own application expects:

Example:

```sh
./build/quick_start /dev/ttyUSB0        # Linux/macOS
```

```powershell
.\build\Release\quick_start.exe COM3   # Windows
```

## Try it on your gripper

The quickest way to confirm your toolchain, your adapter and your
gripper all work together is to build this repository and run one of
the examples it ships — no project of your own needed yet. Clone it
anywhere; this is a throwaway check, not the copy you vendor.

```sh
git clone https://github.com/robotiq/grippers
cmake -S grippers/sdk_cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

Carrying both `CMAKE_BUILD_TYPE` and `--config` makes one pair of
commands work everywhere, since each generator ignores the one it has
no use for.

The unit tests need no hardware, so run them first: they tell a broken
toolchain apart from a wiring problem.

```sh
ctest --test-dir build -C Release
```

Then plug the gripper in and run `move_gripper` with its port, found
the way [Quick start](02-quick-start.md) describes.

```sh
./build/examples/move_gripper /dev/ttyUSB0
```

```powershell
.\build\examples\Release\move_gripper.exe COM3
```

It activates the gripper and then opens and closes the fingers, so keep
the jaws clear before starting it. The other examples are described in
[examples/README.md](../sdk_cpp/examples/README.md).

## Instructions to set up VS Code

1. Install the **C/C++** and **CMake Tools** extensions (both
   publisher `ms-vscode`).
2. Command Palette → **CMake: Select a Kit**. CMake Tools scans for
   what's installed, so on Windows pick the Visual Studio 2022 kit
   whose name ends in `amd64` — the edition in it depends on whether
   you installed the Build Tools or full Visual Studio. On Linux/macOS
   pick whichever kit it finds for your system compiler. Nothing needs
   registering by hand.
3. **CMake: Configure**, then **CMake: Build** (or the matching buttons
   in the status bar at the bottom of the window).
4. To run your own target, once it's built: select it as the active
   target in the status bar's target picker (this also happens
   automatically the first time you build it), then click **Run** (▷)
   in the status bar, or open a terminal and run the built `.exe`
   directly. Either way, if your program takes arguments (e.g. a
   serial port like `COM3`), set them once in `cmake.debugConfig.args`
   in your own `.vscode/settings.json` (`.vscode/` is gitignored except
   for `extensions.json`, so this file is yours alone — create it if it
   doesn't exist yet) and the status bar's Run/Debug buttons will pass
   them automatically.

   ```json
   {
      "cmake.debugConfig.args": [
         "COM3"
         // "/dev/ttyUSB0"
      ]
   }
   ```

> **Note:**
> You can develop and test without a physical gripper using a fake gripper
> object — see [Without a gripper](03-how-it-works.md#without-a-gripper).

## Serial port settings

- **Linux**: add yourself to the `dialout` group for `/dev/ttyUSB*` access:

  ```sh
  sudo usermod -aG dialout $USER
  ```
  Log out and back in (or reboot) for the new group membership to take
  effect.

  > **Warning:**
  >
  > The SDK sets the FTDI `latency_timer` to 1 ms automatically when it has
  > permission (the kernel default of 16 ms triples Modbus latency); for
  > unprivileged use, ship a udev rule that sets it at plug time.
- **Windows**: the FTDI latency timer is a driver setting (Device Manager →
  COM port → Port Settings → Advanced → Latency Timer); set it to 1 ms for
  high-rate control.
  To run what you built on another machine — a cell PC with no compiler on
  it — either install the [Visual C++
  Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) there, or
  configure with `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` to fold the
  runtime into the executable and copy nothing but the `.exe`.
- **macOS**: the FTDI latency timer defaults to 16 ms — capping the exchange
  rate near ~60 Hz — and macOS offers no way to lower it from the SDK. To run
  faster, install [FTDI's VCP driver](https://ftdichip.com/drivers/vcp-drivers/)
  and set its `LatencyTimer` to `1` (in the driver's `Info.plist`); it then
  applies to every open, including this SDK's. On macOS 11+ also approve the
  driver in System Settings → Privacy & Security and make sure it — not
  Apple's built-in FTDI driver — binds your adapter (`kextstat | grep -i ftdi`).
  Otherwise ~60 Hz is the ceiling on the default driver.
  > **Unproven:** this procedure hasn't been verified on real hardware; if you
  > try it, please report back with what worked (or didn't).
- Factory-default link settings: 115200 baud, 8N1, Modbus slave 0x09.
- Port naming: `/dev/ttyUSB0` on Linux, `COM3` on Windows,
  `/dev/tty.usbserial-XXXX` on macOS.

### Gripper on a UR tool connector

A gripper wired to a Universal Robots tool connector is driven from your computer, with the SDK built as above; nothing is compiled for or installed on the robot besides a URCap. The robot serves the tool's RS-485 on TCP port 54321, and `socat` turns it into a local serial port on the computer.

1. On the robot, install the [RS485 URCap](https://github.com/UniversalRobots/Universal_Robots_ToolComm_Forwarder_URCap) and uninstall the Robotiq Grippers URCap, which otherwise holds the tool port.
2. In Installation → General → Tool I/O, set *Controlled by* to *User*, select *Communication Interface* with 115200 baud, no parity, one stop bit, and set the tool output voltage to 24 V.
3. On the computer that runs the SDK, forward the port and pass `/tmp/ttyUR` as the serial port:

   ```sh
   socat pty,link=/tmp/ttyUR,raw,ignoreeof,waitslave tcp:<robot-ip>:54321
   ```

The first status read may time out once while socat connects; the SDK retries it.

## CMake options

[`sdk_cpp/CMakeLists.txt`](../sdk_cpp/CMakeLists.txt) exposes the
`option()`s below. The defaults are right for a normal desktop build — you
adjust them when you're consuming the SDK differently: as a dependency
that shouldn't build its own examples/tests, or on a target that
doesn't have a hosted C++ runtime (see
[Embedded / bare-metal builds](05-embedded-builds.md) for that case
in detail).

| Option | Default | What it controls |
|---|---|---|
| `GRIPPERS_BUILD_EXAMPLES` | `ON` when top-level, `OFF` via `add_subdirectory()` | Builds the example programs under `examples/` — see [examples/README.md](../sdk_cpp/examples/README.md) for what each one does. |
| `GRIPPERS_BUILD_TESTS` | `ON` when top-level, `OFF` via `add_subdirectory()` | Builds and registers the unit tests with CTest. |
| `GRIPPERS_WARNINGS_AS_ERRORS` | `ON` when top-level, `OFF` via `add_subdirectory()` | Treats compiler warnings as errors. Only a request for warnings (not a build break) when consumed through `add_subdirectory()`, since a consumer's newer compiler may warn about something this project's own CI compiler does not. |
| `GRIPPERS_HOSTED` | `ON` | Whether the target has a hosted C++ runtime (`std::thread`, `iostream`). `ON` compiles the `std::thread`-backed `Platform` (`makeDefaultPlatform()`) and the stderr default logger, and links `Threads::Threads`. |
| `GRIPPERS_BUILD_FAKE` | follows `GRIPPERS_HOSTED` | Builds [`makeFakeGripper()`](03-how-it-works.md#without-a-gripper) and the fake device it drives. ~30 KB; only useful to hosted consumers, since the fake device needs the threaded exchange loop to run. |
| `GRIPPERS_BUILD_DEFAULT_SERIAL` | follows `GRIPPERS_HOSTED` | Builds the OS-backed `DefaultSerial` (termios on Linux/macOS, the Win32 comm API on Windows) and the `ConnectionConfig`-based constructors that use it. |

"Top-level" means configuring `sdk_cpp` directly
(`cmake -S sdk_cpp -B build ...`) rather than through
`add_subdirectory()` — that's the case when building the SDK repository
itself, not when consuming it, so it doesn't apply to the
[vendoring setup](#bring-the-sdk-into-your-project) above: when your
own project pulls the SDK in with
`add_subdirectory(path/to/grippers/sdk_cpp)`, GRIPPERS_BUILD_EXAMPLES,
GRIPPERS_BUILD_TESTS, and GRIPPERS_WARNINGS_AS_ERRORS all default to OFF
automatically, so your build doesn't also compile this repo's example and
test binaries, or break on a warning only your compiler raises; turn any
of them back on explicitly if you want that anyway
(`-DGRIPPERS_BUILD_TESTS=ON`).

`GRIPPERS_BUILD_FAKE` and `GRIPPERS_BUILD_DEFAULT_SERIAL` both need
`GRIPPERS_HOSTED=ON` — configuring with one of them `ON` while
`GRIPPERS_HOSTED=OFF` is a `FATAL_ERROR`, not a silent downgrade, since
neither can actually be satisfied without the hosted runtime. Similarly,
turning `GRIPPERS_BUILD_DEFAULT_SERIAL` `OFF` on an otherwise hosted
build doesn't error, but examples and tests need it (they exercise the
real serial transport), so they're skipped with a `message(STATUS ...)`
rather than built:

```sh
cmake -S sdk_cpp -B build -DGRIPPERS_BUILD_DEFAULT_SERIAL=OFF
# -- grippers: examples need GRIPPERS_BUILD_DEFAULT_SERIAL; skipping them
# -- grippers: tests need GRIPPERS_BUILD_DEFAULT_SERIAL; skipping them
```

You'd flip GRIPPERS_BUILD_DEFAULT_SERIAL off, on an otherwise-hosted desktop
build, if you're injecting your own `Serial` — talking to the gripper
over a TCP-to-serial bridge, say, rather than a local COM port.
