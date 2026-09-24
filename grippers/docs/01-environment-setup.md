# Environment setup
This page covers setting up a project that uses the Robotiq gripper C++
SDK as a dependency: installing prerequisites, bringing the SDK into your
own build.

CMake ≥ 3.16, a C++17 compiler, libserialport.

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
| Windows | MSYS2 — see [Windows](#windows) below |

### Linux and macOS

The installation of the libserialport library on Linux or macOS is straightforward.

Install libserialport:
```sh
sudo apt install libserialport-dev   # Ubuntu/Debian
brew install libserialport           # macOS
```

### Windows

On Windows the libserialport library used by the C++ driver requires using
MSYS2 for its compilation. MSYS2 is a Linux-like terminal which can run GCC,
CMake and Ninja.

1. Install MSYS2 from [msys2.org](https://www.msys2.org)
   (or `winget install MSYS2.MSYS2`).
2. Open the **MSYS2 UCRT64** shell from the Start menu.
3. Install the toolchain and dependencies:

   ```sh
   # Synchronize package databases and upgrade all installed packages to their
   # latest versions
   pacman -Syu

   # Install the core development tools for the 64-bit UCRT
   # (Universal C Runtime) environment, skipping any packages that are already
   # up to date (--needed)
   pacman -S --needed \
            mingw-w64-ucrt-x86_64-gcc \
            mingw-w64-ucrt-x86_64-cmake \
            mingw-w64-ucrt-x86_64-ninja \
            mingw-w64-ucrt-x86_64-libserialport \
            mingw-w64-ucrt-x86_64-gdb
   ```

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

> **Windows**: run from the **MSYS2 UCRT64** shell (Start menu), replacing
> step 1 above with:
> 
> ```sh
> cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
> ```
>
> Step 2 (`cmake --build build -j`) is unchanged.

Your application will then be compiled and you will be able to launch it
passing whatever arguments your own application expects:

Example:

```sh
./build/quick_start /dev/ttyUSB0        # Linux/macOS
./build/quick_start.exe COM3            # Windows
```

## Instructions to set up VS Code

1. Install the **C/C++** and **CMake Tools** extensions (both
   publisher `ms-vscode`).
2. **Windows only** — as mentioned above, you need the MSYS2 GCC
   toolchain.

   Register a compilation kit once via Command Palette →
   **CMake: Edit User-Local CMake Kits**, so it's offered in every
   workspace on your machine:
   
   ```json
   {
     "name": "MSYS2 UCRT64 GCC",
     "compilers": {
       "C": "C:/msys64/ucrt64/bin/gcc.exe",
       "CXX": "C:/msys64/ucrt64/bin/g++.exe"
     },
     "preferredGenerator": { "name": "Ninja" },
     "cmakeSettings": {
       "SERIALPORT_LIBRARY": "C:/msys64/ucrt64/lib/libserialport.a",
       "CMAKE_EXE_LINKER_FLAGS": "-static -static-libgcc -static-libstdc++",
       "CMAKE_CXX_STANDARD_LIBRARIES": "-lsetupapi -lcfgmgr32"
     }
   }
   ```

3. Command Palette → **CMake: Select a Kit**.
   On Windows, pick the **MSYS2 UCRT64 GCC** kit registered above.
   On Linux/macOS, pick whichever kit CMake Tools finds for your system compiler.
4. **CMake: Configure**, then **CMake: Build** (or the matching buttons
   in the status bar at the bottom of the window).
5. To run your own target, once it's built: select it as the active
   target in the status bar's target picker, then click **Run** (▷)
   in the status bar, or open a terminal and run the built executable
   directly.
   
   If your program takes arguments (e.g. a serial port like `COM3`), set them
   once in `cmake.debugConfig.args` in your own `.vscode/settings.json`
   and the status bar's Run/Debug buttons will pass them automatically.

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
| `GRIPPERS_BUILD_DEFAULT_SERIAL` | follows `GRIPPERS_HOSTED` | Builds the libserialport-backed `DefaultSerial` and the `ConnectionConfig`-based constructors that use it. |

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
build, if you're injecting your own `Serial` (e.g. talking to the gripper through
something other than libserialport) and don't want the libserialport
dependency at all.
