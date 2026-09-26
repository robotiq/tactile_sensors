# How it works

Robotiq grippers are controlled by writing commands to, and reading status
from, their memory over Modbus RTU. With this SDK, you never issue Modbus RTU
requests yourself: you call `setCommand()` and `getStatus()`, and the `Gripper`
object handles the Modbus RTU exchange with the hardware in the background.
A thread that continuously exchanges FC 0x17 Modbus (read&write) transactions
with the gripper, up to ~250 Hz at 115200 baud.

The C++ driver's design is built around maximizing that communication frequency.

```
                      ┌───────────────┐                              ┌───────────────┐
setCommand() ────────►│    Gripper    │         250 Hz loop          │    Gripper    │
                      │     object    │◄────────────────────────────►│               │
getStatus() ◄─────────│               │     FC 0x17 read & write     │               │
                      └───────────────┘                              └───────────────┘
```

Each completed exchange leaves a record, `StampedExchange`: the command
block it wrote, the status block it read back, and the count and instant
that name it. `getMostRecentStampedExchange()` returns the latest;
`waitForExchange()` blocks until the next one completes and returns it.
A control loop that must act on every exchange exactly once uses
`waitForExchangeCount()` instead, asking for one past the exchange it
last acted on: nothing completed in between is skipped, and a loop that
fell behind catches up on the newest at once.
[`sdk_cpp/examples/exchange_log.cpp`](../sdk_cpp/examples/exchange_log.cpp)
records every exchange to a CSV file that way.

> **Note:**
>
> The exchange thread's Modbus protocol layer is
> [nanoMODBUS](https://github.com/debevv/nanoMODBUS) (vendored under
> `sdk_cpp/third_party/`, BSD-licensed); on a hosted build, its serial
> transport is [libserialport](https://sigrok.org/wiki/Libserialport).

> **Note:**
>
> Check the gripper user manual on the Robotiq support website if you want to
> learn more about the gripper's Modbus RTU communication.

## Command

The registers used to command the gripper are composed of 3 16-bit registers.
Each register is split into 2 bytes, for a total of 6 bytes:

|Register| Byte | Name | Content |
|---|---|---|---|
|1000| 0 | ACTION REQUEST | Packed Byte |
|1000| 1 | Reserved | - |
|1001| 2 | Reserved | - |
|1001| 3 | POSITION REQUEST (rPR) | 0-255 value |
|1002| 4 | SPEED (rSP) | 0-255 value |
|1002| 5 | FORCE (rFR) | 0-255 value |

The gripper action request byte is a packed bit field.

| Bits | 7-6 | 5 | 4 | 3 | 2-1 | 0 |
|---|---|---|---|---|---|---|
| Field | reserved | rARD | rATR | rGTO | reserved | rACT |

This SDK translates this into a `command` with equivalent fields.

Packed bytes are built using the `set` function and a dedicated enum.

<!-- snippet: snippets.cpp command-action-bits -->
```cpp
// Command object sent to the gripper.
Robotiq::GripperCommand command = Robotiq::GripperCommand::defaults();

// Command building blocks
// ACTION - Activate - rACT
command.action.set(Robotiq::ActionRequestBit::Activate, true);
// ACTION - Goto - rGTO
command.action.set(Robotiq::ActionRequestBit::GoTo, true);
// ACTION - Auto Release - rATR
command.action.set(Robotiq::ActionRequestBit::AutoRelease, false);
// ACTION - Auto Release Open Direction - rARD
command.action.set(Robotiq::ActionRequestBit::AutoReleaseOpenDirection, true);
// POSITION REQUEST - Position Request - rPR
command.positionRequest = 100;
// SPEED - Speed - rSP
command.speed = 255;
// FORCE - Force - rFR
command.force = 255;
```

Each of these bits is set with `command.action.set(Robotiq::ActionRequestBit::<name>, value)`; `value` defaults to `true` when omitted:

| Bit | `ActionRequestBit` | `false` | `true` |
|---|---|---|---|
| rACT | `Activate` | Deactivate gripper. | Activate gripper (must stay on after activation routine is completed). |
| rGTO | `GoTo` | Stop. | Go to requested position. |
| rATR | `AutoRelease` | Normal. | Emergency auto-release. |
| rARD | `AutoReleaseOpenDirection` | Closing auto-release direction. | Opening auto-release direction. |

### Commanding in SI units

The SDK offers the capability to set position and speed in SI units rather than
encoded bits.

It works with gripper profiles which gather gripper specifications and SI
units conversion functions. The mappings are linear and approximate.

Some gripper profiles are provided for convenience. You may have to create your
own profile if for example you are using custom fingers and the close and open
position are different from the default profiles.

Force is commanded as an effort between 0 (minimum) and 1 (maximum) rather
than newtons.

> **Note:**
> The actual grip force is a function of the speed register, the force
> register, the hardness of the fingers and object material. As a
> consequence, the force register cannot be converted into a value in newtons.

<!-- snippet: snippets.cpp si-unit-conversion -->
```cpp
using Robotiq::profiles::k2F85;
namespace units = Robotiq::units;

constexpr double kSpeed = 0.150; // m/s
constexpr double kOpening = 0.040; // m
constexpr double kEffort = 0.5;

Robotiq::GripperCommand command = Robotiq::GripperCommand::defaults();
command.speed = units::speedToRegister(kSpeed, k2F85).value();
command.force = units::effortToRegister(kEffort).value();
command.positionRequest = units::openingToRegister(kOpening, k2F85).value();

double openingMetres = units::openingFromRegister(gripper.getStatus().position, k2F85).value();
```

These conversions return `std::optional` and yield nothing for a quantity with
no defensible register value, so `.value()` above is safe only because `k2F85`
is a well-formed profile. A hand-written profile deserves a check; see
`moveTo()` in the [Robust example walkthrough](04-robust-example-walkthrough.md#checking-si-unit-conversions-before-asserting).

## Status

The registers used to retrieve the status of the gripper are composed of
3 16-bit registers. Each register is split into 2 bytes, for a total of 6
bytes.

|Register| Byte | Name | Content |
|---|---|---|---|
|2000| 0 | GRIPPER STATUS | Packed Byte |
|2000| 1 | Reserved | - |
|2001| 2 | FAULT STATUS | Packed Byte |
|2001| 3 | POS REQUEST ECHO (gPR) | 0-255 value |
|2002| 4 | POSITION (gPO) | 0-255 value |
|2002| 5 | CURRENT (gCU) | 0-255 value |

The gripper status and fault status bytes are packed bit fields.

*GRIPPER STATUS BYTE (0)*

| Bits | 7-6 | 5-4 | 3 | 2-1 | 0 |
|---|---|---|---|---|---|
| Field | gOBJ | gSTA | gGTO | reserved | gACT |

*FAULT STATUS BYTE (2)*

| Bits | 7-4 | 3-0 |
|---|---|---|
| Field | kFLT | gFLT |

This SDK translates this into a status object with equivalent fields.

<!-- snippet: snippets.cpp status-gripper-status-fields -->
```cpp
// Status object received from the gripper
Robotiq::GripperStatus status = gripper.getStatus();

// Status building blocks
// GRIPPER STATUS - Object detection - gOBJ
Robotiq::ObjectDetection gOBJ = status.gripperStatus.objectDetection();
// GRIPPER STATUS - Activation State - gSTA
Robotiq::ActivationState gSTA = status.gripperStatus.activationState();
// GRIPPER STATUS - Goto Enabled - gGTO
bool gGTO = status.gripperStatus.goToEnabled();
// GRIPPER STATUS - Activated - gACT
bool gACT = status.gripperStatus.activated();
// FAULT STATUS - Controller Fault - kFLT
Robotiq::ControllerFault kFLT = status.faultStatus.controllerFault();
// FAULT STATUS - Gripper Fault - gFLT
Robotiq::GripperFault gFLT = status.faultStatus.gripperFault();
// POS REQUEST ECHO - Position Request Echo - gPR
uint8_t gPR = status.positionRequestEcho;
// POSITION - Position - gPO
uint8_t gPO = status.position;
// CURRENT - Current - gCU
uint8_t gCU = status.current;
```

`gOBJ`, `gSTA` and `gFLT` are not plain numbers: each is decoded into its own enum, listed below with every possible value.

**gOBJ — `Robotiq::ObjectDetection`** (only meaningful while `gGTO` is set)

| Value | Meaning |
|---|---|
| `Moving` (`0x00`) | Fingers are in motion towards requested position. No object detected. |
| `DetectedWhileOpening` (`0x01`) | Fingers have stopped due to a contact while opening before requested position. Object detected. |
| `DetectedWhileClosing` (`0x02`) | Fingers have stopped due to a contact while closing before requested position. Object detected. |
| `AtRequestedPosition` (`0x03`) | Fingers are at requested position. No object detected, or object has been lost/dropped. |

**gSTA — `Robotiq::ActivationState`**

| Value | Meaning |
|---|---|
| `Reset` (`0x00`) | Gripper is in reset (or automatic release) state. Check `gFLT` if the gripper is activated. |
| `InProgress` (`0x01`) | Activation in progress. |
| `Reserved` (`0x02`) | Not used. |
| `Complete` (`0x03`) | Activation is completed. |

**gFLT — `Robotiq::GripperFault`**

| Value | Meaning |
|---|---|
| `None` (`0x00`) | No fault (solid blue status LED). |
| `ActionDelayed` (`0x05`) | Action delayed: the activation (re-activation) must be completed prior to performing the action. |
| `ActivationRequired` (`0x07`) | The activation bit (rACT) must be set prior to performing the action. |
| `OverTemperature` (`0x08`) | Maximum operating temperature exceeded (≥ 85 °C internally); let it cool down (below 80 °C). |
| `NoCommunication` (`0x09`) | No communication during at least 1 second. |
| `UnderVoltage` (`0x0A`) | Under minimum operating voltage.¹ |
| `AutomaticReleaseInProgress` (`0x0B`) | Automatic release in progress.¹ |
| `InternalFault` (`0x0C`) | Internal fault; contact support@robotiq.com.¹ |
| `ActivationFault` (`0x0D`) | Activation fault; verify that no interference or other error occurred.¹ |
| `Overcurrent` (`0x0E`) | Overcurrent triggered.¹ |
| `AutomaticReleaseComplete` (`0x0F`) | Automatic release completed.¹ |

¹ Major fault (status LED blinking red/blue): call [`recoverFromFault()`](#recovering-from-a-fault) — a reset (rising edge on rACT) is required to clear it.

**kFLT — `Robotiq::ControllerFault`**

These are the codes reported by the optional Robotiq Universal Controller:

| Value | Meaning |
|---|---|
| `None` (`0x00`) | No fault. |
| `Supply24VNotDetected` (`0x04`) | 24V supply not detected; reconfiguration over USB is still possible. |
| `NoDeviceDetected` (`0x05`) | No gripper detected on the bus. |
| `CommunicationNotReady` (`0x09`) | Main communication protocol is booting. |
| `EmergencyStop` (`0x0C`) | Emergency stop engaged. |
| `Overcurrent` (`0x0E`) | Controller overcurrent protection tripped. |

Example of how to use the object detection value:

<!-- snippet: snippets.cpp status-object-detection-check -->
```cpp
Robotiq::ObjectDetection gOBJ = gripper.getStatus().gripperStatus.objectDetection();
if(gOBJ == Robotiq::ObjectDetection::Moving)
{
   // fingers are still moving towards the requested position
}
```

### Human-readable output

The SDK includes a `toString()` function to translate command and status information in human-readable format.

<!-- snippet: snippets.cpp status-to-string -->
```cpp
std::string rendered = Robotiq::toString(gripper.getStatus());
std::cout << rendered << std::endl;
```
Here is an example of what gets printed:
```bash
gACT=1 gGTO=1 gSTA=Complete(0x3) gOBJ=AtRequestedPosition(0x3) gFLT=None(0x0) kFLT=None(0x0) gPR=100 gPO=100 gCU=0
```

## Gripper-related functions

`Gripper`'s own methods — `setCommand()`, `getStatus()`, `getCommand()`,
`connectionState()` — are used to set the command or read the status of the gripper.

> **Warning:** calling `setCommand()` does not necessarily mean the command
> will effectively be sent to the gripper.

These functions only read or write a local copy of the gripper's command and
status blocks, owned by the gripper object. They do not send any Modbus RTU
command to the gripper. The Modbus RTU communication is managed in the
background by the gripper object.

As a consequence, if you write back-to-back `setCommand()` instructions, only
the latest one will be taken into account.

As an example, the code below sets back to back an autorelease command followed
by a move command. The autorelease command will be overwritten by the move
command without taking effect.

<!-- snippet: snippets.cpp autorelease-then-move -->
```cpp
// Build and set an autorelease command
Robotiq::GripperCommand command = Robotiq::GripperCommand::defaults();
command.action.set(Robotiq::ActionRequestBit::AutoRelease);
gripper.setCommand(command);

// Build and set a command to move the gripper to the position 100
command.action.set(Robotiq::ActionRequestBit::GoTo);
command.positionRequest = 100;
gripper.setCommand(command);
```

> **Note :**
> Note that the same command object is used for the second setCommand. This
> is good practice to retain the previously set parameters.

To have the autorelease command effectively sent to the gripper, it is
necessary to wait for the gripper to acknowledge reception of the command
before the next `setCommand()`.

<!-- snippet: snippets.cpp autorelease-then-move-with-wait -->
```cpp
// Build and set an autorelease command
Robotiq::GripperCommand command = Robotiq::GripperCommand::defaults();
command.action.set(Robotiq::ActionRequestBit::AutoRelease);
gripper.setCommand(command);

// Wait
Robotiq::waitFor(
   [&] {
      return (gripper.getStatus().faultStatus.gripperFault() == Robotiq::GripperFault::AutomaticReleaseInProgress);
   },
   10s);

// Build and set a command to move the gripper to the position 100
command.action.set(Robotiq::ActionRequestBit::GoTo);
command.positionRequest = 100;
gripper.setCommand(command);
```

The following section presents the wait function used in the code above.

### Waiting for a condition

`waitFor()` (and `waitUntil()`, which takes a deadline instead of a
timeout) polls a predicate until it becomes true or the timeout
elapses:

<!-- snippet: snippets.cpp wait-for-motion-settled -->
```cpp
bool settled = Robotiq::waitFor(
   [&] { return gripper.getStatus().gripperStatus.objectDetection() != Robotiq::ObjectDetection::Moving; },
   10s);
```

In some cases, it helps to wait for a given gripper state before moving to the next
instruction (wait for the gripper to acknowledge a command, wait for the
gripper to complete its motion, ...).

### Activating the gripper

Before sending motion commands, the gripper must be activated once. During
the activation process the gripper fully opens and closes to detect its
extreme positions.

`activate()` runs the activation handshake:

<!-- snippet: snippets.cpp activate-only -->
```cpp
Robotiq::ActivationResult result = Robotiq::activate(gripper);
```

| `ActivationResult` | Meaning |
|---|---|
| `Activated` | the handshake ran (or one already in progress finished); the gripper is ready |
| `AlreadyActive` | already activated and fault-free; `activate()` did nothing |
| `FaultLatched` | a major fault is latched; `activate()` refuses to reset it — call `recoverFromFault()` instead |
| `Timeout` | the link stayed down, or the handshake never completed in time |

Calling `activate()` on an already-activated gripper is safe and a
no-op, so it's fine to call it at the start of every run rather than
tracking activation state yourself.

### Recovering from a fault

`Robotiq::recoverFromFault()` is used to recover the gripper from a major fault
(see [Error handling](#error-handling) below).

<!-- snippet: snippets.cpp recover-from-fault-only -->
```cpp
Robotiq::ActivationResult result = Robotiq::recoverFromFault(gripper);
```

`recoverFromFault()` resets the gripper and reruns the activation handshake.

> **Warning:** As the gripper opens and closes during the activation process,
> any object inside it will be released.

## Error handling

Constructing a `Gripper` can throw:
- `SerialIOException` — the serial port could not be opened or configured.
- `DriverException` — no gripper answered the initial read, or the
  connection configuration (e.g. `connectionFrequency`) is invalid.

### Checking fault severity

Not every fault needs recovery. `Robotiq::severity()` classifies the
raw fault code from `status.faultStatus` into a `FaultSeverity`:

<!-- snippet: snippets.cpp fault-severity-check -->
```cpp
Robotiq::FaultStatus fault = gripper.getStatus().faultStatus;
if(Robotiq::severity(fault.gripperFault()) == Robotiq::FaultSeverity::Major)
{
   Robotiq::recoverFromFault(gripper);
}
```

| `FaultSeverity` | Meaning |
|---|---|
| `None` | no fault |
| `Warning` | informational; clears on its own |
| `Minor` | degraded operation; clears on its own |
| `Major` | needs a reset (rACT rising edge) to clear — call [`recoverFromFault()`](#recovering-from-a-fault) |

## Control method

The communication flow to control the gripper is typically the following:
- Build a command
- Send the command to the gripper
- Wait for the gripper to acknowledge the command
- Wait for the gripper to complete the action
- Check final status

Build a command:

<!-- snippet: quick_start.cpp qs-create-command -->
```cpp
Robotiq::GripperCommand command = Robotiq::GripperCommand::defaults();
command.action.set(Robotiq::ActionRequestBit::GoTo);
command.positionRequest = 100;
command.speed = 255;
command.force = 255;
```

Send the command:

<!-- snippet: quick_start.cpp qs-send-command -->
```cpp
gripper.setCommand(command);
```

Wait for the gripper to acknowledge the command, then wait for it to complete:

<!-- snippet: quick_start.cpp qs-wait -->
```cpp
// 6- Wait for the gripper to echo
Robotiq::waitFor([&] { return gripper.getStatus().positionRequestEcho == command.positionRequest; }, 1s);

// 7- Wait for the gripper to start moving
Robotiq::waitFor(
   [&] { return (gripper.getStatus().gripperStatus.objectDetection() == Robotiq::ObjectDetection::Moving); },
   200ms);

// 8- Wait for the gripper to stop
Robotiq::waitFor(
   [&] { return (gripper.getStatus().gripperStatus.objectDetection() != Robotiq::ObjectDetection::Moving); },
   5s);
```

Check final status:

<!-- snippet: quick_start.cpp qs-status -->
```cpp
// 9- retrieve status
uint8_t currentPosition = gripper.getStatus().position;

// Print retrieved status
std::cout << "Current position : " << static_cast<int>(currentPosition) << std::endl;
```

## Without a gripper

`makeFakeGripper()` returns a `Gripper` driving a fake device instead of
a serial port, for bring-up, demos, and CI on machines with no hardware
attached:

<!-- snippet: snippets.cpp make-fake-gripper -->
```cpp
auto gripper = Robotiq::makeFakeGripper(); // no hardware needed
Robotiq::activate(*gripper);
```

Everything above the wire is the real thing — the typed blocks, the
exchange cycle, the process image, `activate()` / `recoverFromFault()`.
The device below it is deliberately minimal: activation completes
instantly, and the fingers are wherever they were last commanded to be.
There is no motion profile, no travel time, no object detection, and no
fault injection.

That makes it a good fit for exercising your own integration logic
against the SDK's real API surface — including in CI, with no hardware
attached — but it can't stand in for a physical gripper when what
you're validating is motion timing, object detection, or fault
behavior.

`makeFakeGripper()` is built under the `GRIPPERS_BUILD_FAKE` CMake
option; see [CMake options](01-environment-setup.md#cmake-options).

## Logging

`Gripper`'s constructor takes an optional `logger` parameter (a
`std::shared_ptr<Robotiq::Logger>`). Passing nothing, or explicitly
`nullptr`, doesn't disable logging — it falls back to the SDK's own
default logger: `StderrLogger` on a hosted build, a do-nothing
`NullLogger` on a freestanding target with no console.

<!-- snippet: snippets.cpp basic-logger-injection -->
```cpp
auto logger = std::make_shared<Robotiq::StderrLogger>();
Robotiq::Gripper gripper(config, logger);
```

`Logger` is a one-method interface — `log(Level, message)` — with four severities:

| `Robotiq::Logger::Level` | Meaning |
|---|---|
| `Debug` | high-frequency, diagnostic detail |
| `Info` | normal operational events |
| `Warn` | recoverable, but worth a human's attention |
| `Error` | an operation failed |

The background exchange thread uses this same logger to report its own
health, independently of anything your own code does:
- `Warn` when several consecutive exchanges fail — the same moment
  `connectionState()` switches to `ConnectionState::Faulted`, meaning
  the process image `getStatus()` returns is now stale.
- `Info` when the link recovers and `connectionState()` returns to
  `Operational`.

Passing your own `Logger` lets you route
these lines — and your own application's — through one sink instead of
two independently-timed sources that could interleave confusingly, the
same pattern
[`move_gripper.cpp` uses](04-robust-example-walkthrough.md#naming-the-loggers)
for its own narration.
