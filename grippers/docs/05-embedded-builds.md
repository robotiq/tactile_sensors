# Embedded / bare-metal builds

This page explains how the SDK compiles for a microcontroller target
(STM32-class and similar) that has no operating system underneath it —
no `std::thread`, no filesystem, often no heap of any real size. If
you've only used the SDK on Linux/Windows/macOS so far, read
[Quick start](02-quick-start.md) first for the basic `Gripper`
connect → activate → command → status flow this page builds on;
`Platform` and `Serial` are introduced below.

## The core idea

The parts of the SDK that actually talk Modbus and decode the
command/status blocks don't care what OS they're running on — they're
plain C++17. Everything that *is* OS-specific — spawning a thread,
using a lockable, sleeping, opening a UART — is factored out behind two
small interfaces that `Gripper` is built on top of, instead of being
baked in:

- **`Platform`** ([`platform.hpp`](../sdk_cpp/include/Robotiq/gripper/platform.hpp))
  — the three things the threaded exchange loop needs: a mutex, a
  thread, and a yielding sleep.
- **`Serial`** ([`serial.hpp`](../sdk_cpp/include/Robotiq/gripper/serial.hpp))
  — open/read/write/close on a byte stream.

On desktop, you never see this seam: `Gripper(config)` quietly builds a
`std::thread`-backed `Platform` and a libserialport-backed `Serial` for
you. On a microcontroller, neither of those exist, so you supply your
own implementation of one or both — the SDK's own logic (the exchange
loop, the Modbus framing, the typed command/status decoding) doesn't
change at all.

## Two integration paths

Which of these you want depends on whether your firmware already has
an RTOS running.

### 1. You have an RTOS: implement `Platform`

Keep using `Gripper` exactly as described in
[Quick start](02-quick-start.md) — same `activate()`, `setCommand()`,
`getStatus()` — but construct it with the platform-taking constructor
instead of a `ConnectionConfig`, passing your own `Platform` and
`Serial`.

[`ports/threadx/threadx_platform.hpp`](../sdk_cpp/ports/threadx/threadx_platform.hpp)
is a complete, working `Platform` over Azure RTOS ThreadX, and is the
reference to copy when porting to a different RTOS — it implements
exactly four members:

| `Platform` member | ThreadX primitive underneath |
|---|---|
| `makeMutex()` | `tx_mutex_create` / `tx_mutex_get` / `tx_mutex_put` |
| `spawn(fn)` | `tx_thread_create`, running `fn` on a dedicated task |
| `sleepUntil(timePoint)` | `tx_thread_sleep`, rounded up to whole ThreadX ticks |
| `sleepFor(duration)` | same, duration-based |

Porting to another RTOS (FreeRTOS, Zephyr, etc.) means writing the same
four members over that RTOS's native mutex/task/sleep calls — nothing
about `Gripper` itself needs to change.

Two integration mistakes are worth knowing about up front, both
because they *don't* fail loudly — they just hang:

- **Your `Serial::read()` must yield the CPU while it waits for
  bytes.** Feed it from a UART interrupt or DMA completion signalled
  through an RTOS semaphore/event. A busy-wait read (e.g. a polled
  `HAL_UART_Receive`) runs at the exchange thread's priority and
  starves every lower-priority task — including the task that's
  waiting on `activate()` — so the whole application appears to hang,
  not just crash.
- **`std::chrono::steady_clock` must actually advance.** Its backing
  call (`clock_gettime(CLOCK_MONOTONIC)` or `gettimeofday()`, depending
  on your toolchain's C library) needs a real implementation on a
  freestanding target; if it's stubbed out, every timeout in the SDK —
  `activate()`, `recoverFromFault()`, the exchange loop's own pacing —
  silently stops advancing instead of erroring. This repo's CI
  disassembles `libstdc++`'s `chrono.o` to find out which call your
  specific toolchain/multilib needs (see the "Assert the steady_clock
  backing" step in
  [`.github/workflows/ci.yml`](../.github/workflows/ci.yml)) and
  implements it in
  [`ports/threadx/link_test/newlib_glue.c`](../sdk_cpp/ports/threadx/link_test/newlib_glue.c)
  — do the same check for your target rather than assuming.

[`ports/threadx/link_test/main.cpp`](../sdk_cpp/ports/threadx/link_test/main.cpp)
is the smallest complete example of wiring this together: it builds a
`ThreadXPlatform`, a stub `Serial`, constructs a real `Gripper` on top
of them, and calls `activate()` — a good starting skeleton to copy from.

### 2. No RTOS at all: skip `Gripper`, drive `GripperModbusClient` yourself

If your firmware is a bare superloop (one `while(true)` that services
everything in turn, no scheduler), don't implement `Platform` at all —
there's no thread for it to create. Instead, use
[`detail::GripperModbusClient`](../sdk_cpp/include/Robotiq/detail/gripper_modbus_client.hpp)
directly: it's the layer immediately below `Gripper`, doing exactly one
Modbus transaction per call (`readStatus()`, `writeCommand()`, or
`exchange()` for both at once) and needing no `Platform` and no
background thread, because *your* superloop is what decides when the
next transaction happens — you call `exchange()` once per iteration,
in place of `Gripper`'s background thread doing it on a timer.

This is the simpler path when it fits: no RTOS to port to, no mutex, no
"is my `Serial::read` yielding correctly" to get right. The tradeoff is
that it's a lower-level API — no `getCommand()`/`getStatus()` snapshot
to read from another task, because there's only ever one task.

## The CMake side

Two options (of the five documented in
[Environment setup](01-environment-setup.md#cmake-options))
matter here:

```sh
cmake -S sdk_cpp -B build-freestanding \
  -DGRIPPERS_HOSTED=OFF \
  -DGRIPPERS_BUILD_DEFAULT_SERIAL=OFF
```

- **`GRIPPERS_HOSTED=OFF`** leaves out the `std::thread`-backed
  `Platform` and the stderr default logger entirely — those
  translation units aren't even compiled, so nothing pulls in
  `<thread>` or `Threads::Threads`. Use the platform-taking constructor
  and supply your own `Platform` (path 1) or skip `Gripper` for
  `GripperModbusClient` (path 2).
- **`GRIPPERS_BUILD_DEFAULT_SERIAL=OFF`** leaves out the
  libserialport-backed `Serial`, and the libserialport dependency along
  with it — there's no desktop serial port on a microcontroller anyway.
  Supply your own `Serial` over your UART.

`GRIPPERS_BUILD_FAKE` turns itself off automatically when
`GRIPPERS_HOSTED=OFF` (it needs the threaded exchange loop to have
something to run against), and setting it `ON` explicitly alongside
`GRIPPERS_HOSTED=OFF` is a hard configure error rather than a silent
no-op — see
[Environment setup](01-environment-setup.md#cmake-options)
for the full table.

For an actual cross-compile, add your toolchain's flags on top. The
flags this repo's CI uses to compile-check the core for a Cortex-M55
target (from the `cortex-m55` job in
[`.github/workflows/ci.yml`](../.github/workflows/ci.yml)) are a
reasonable starting point:

```sh
arm-none-eabi-g++ -std=c++17 -Isdk_cpp/include -Isdk_cpp/third_party/nanomodbus \
  -mcpu=cortex-m55 -mthumb -fexceptions -Wall -Wextra -Werror \
  -DGRIPPERS_HOSTED=0 -DGRIPPERS_BUILD_DEFAULT_SERIAL=0 \
  -c -o gripper.o sdk_cpp/src/gripper.cpp
```

`-fexceptions` is required, not optional: the exchange loop and the
Modbus client both use exceptions for error reporting (`DriverException`,
`SerialIOException`), the same as on desktop.

## How CI actually verifies this

Two separate CI jobs back the claims on this page, which is worth
knowing if you're deciding how much to trust them:

- **`cortex-m55`** — compiles the freestanding core (`gripper.cpp`, the
  Modbus client, the freestanding logger) with the exact flags above.
  This is *compile-only*: no runner can execute Cortex-M55 code, so it
  proves the source is freestanding-clean, not that it links.
- **`threadx-link`** — actually links the full threaded stack
  (`Gripper` on `ThreadXPlatform`) against a real build of ThreadX for
  Cortex-M55, and asserts that expected symbols
  (`Gripper::Gripper`, `ThreadXPlatform::spawn`, `_tx_thread_create`,
  `__cxa_throw`) are present in the final image. This is what catches
  problems a compile can't: missing symbols, the unwinder
  `-fexceptions` pulls in, and the `steady_clock` backing discussed
  above. It also cross-checks that the toolchain's `chrono.o` needs
  `gettimeofday` or `clock_gettime` and fails loudly if it needs
  neither (meaning `newlib_glue.c` would need a third implementation).

Neither job flashes or runs anything on real hardware — signing and
on-device testing live outside this repo — but between them, "does it
compile freestanding" and "does the threaded stack actually link
against a real RTOS" are both continuously checked, not just asserted
in prose.
