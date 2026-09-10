# Robust example walkthrough

[Quick start](02-quick-start.md) shows the bare minimum to move a
gripper and intentionally skips error handling. [`move_gripper.cpp`](../sdk_cpp/examples/move_gripper.cpp)
is the same connect → activate → command → wait → status flow made
robust, using the functions from [How it works](03-how-it-works.md).
This page doesn't re-walk that flow — it covers what's different about
the robust version and why.

## Argument handling

<!-- snippet: move_gripper.cpp argument-handling -->
```cpp
if(argc < 2)
{
   std::cerr << "Usage: " << argv[0] << " <port> [baudrate]\n";
   return EXIT_FAILURE;
}
```

The port is required; the baudrate is optional and defaults to
`ConnectionConfig`'s own default (115200) if omitted. The accepted range
is bounded at both ends:

<!-- snippet: move_gripper.cpp baudrate-bounds -->
```cpp
constexpr unsigned long kMinBaudrate = 1;
constexpr unsigned long kMaxBaudrate = 1000000;
```

When a baudrate is given, it's parsed and range-checked against those
bounds before use:

<!-- snippet: move_gripper.cpp baudrate-parse-and-check -->
```cpp
const unsigned long parsed = std::stoul(argv[2]);
if(parsed < kMinBaudrate || parsed > kMaxBaudrate)
{
   throw std::out_of_range("baudrate outside the supported range");
}
```

The lower bound isn't just documentation-by-example: `std::stoul("-1")`
doesn't throw, it silently wraps around to a huge unsigned value.
Without the `< kMinBaudrate` check, a typo'd negative baudrate would
sail past `std::stoul` and only fail (or misbehave) much later, further
from the actual mistake.

## Reporting a failed connection

Constructing `Gripper` can throw (`SerialIOException` or
`DriverException` — see [Error handling](03-how-it-works.md#error-handling)).
The example catches it once, right at construction, and turns it into a
checklist instead of a raw exception message:

<!-- snippet: move_gripper.cpp connection-error-checklist -->
```cpp
catch(const std::exception& ex)
{
   std::cerr << "Error: " << ex.what() << "\n\n"
             << "Could not open a gripper on '" << argv[1] << "'. Check that:\n"
             << "  - the gripper is connected and powered;\n"
             << "  - the port name is correct (Linux /dev/ttyUSB0, macOS /dev/tty.usbserial-*, Windows COM3);\n"
             << "  - you have permission to use it (Linux: join the 'dialout' group).\n";
   return EXIT_FAILURE;
}
```

That checklist covers the three things that actually cause this
exception in practice: nothing plugged in, the wrong port name for the
platform, and (Linux only) not being in the `dialout` group — see
[Serial port notes](01-environment-setup.md#serial-port-notes).

## Naming the loggers

The SDK logs through an injectable `Logger`. The example creates one,
named `"example"`, for its own narration ("Activating...", "Opening...",
"Closing..."):

<!-- snippet: move_gripper.cpp logger-example-name -->
```cpp
auto logger = std::make_shared<Robotiq::StderrLogger>("example");
```

...and a second, separately-named `"robotiq"` instance passed directly
to the `Gripper` constructor, for the SDK's own internal logging:

<!-- snippet: move_gripper.cpp logger-robotiq-name -->
```cpp
gripper =
   std::make_unique<Gripper>(config,
                             std::make_shared<Robotiq::StderrLogger>("robotiq")); // opens and starts exchanging
```

Both still write to the same stream (stderr), so this isn't about
separating *where* the lines go — it's what keeps them told apart once
they're there: a reader (or a log aggregator) can tell the SDK's own
diagnostic lines apart from the example's own progress messages, instead
of both landing unlabeled and easy to confuse with each other.

## Handling a latched fault at activation

Quick start's activation note shows how to force a gripper into a
deactivated state. `move_gripper.cpp` instead handles the case where
activation can't proceed at all because a fault is already latched:

<!-- snippet: move_gripper.cpp activation-recovery -->
```cpp
ActivationResult activation = Robotiq::activate(*gripper);
if(activation == ActivationResult::FaultLatched)
{
   // Recovery releases any grip and sweeps the fingers, so the SDK
   // never runs it implicitly; this example has no part to drop.
   logger->log(Robotiq::Logger::Level::Warn, "fault latched; recovering (the fingers will move)");
   activation = Robotiq::recoverFromFault(*gripper);
}
```

<!-- snippet: move_gripper.cpp activation-final-check -->
```cpp
if(activation != ActivationResult::Activated && activation != ActivationResult::AlreadyActive)
{
   logger->log(Robotiq::Logger::Level::Error, withStatus("activation failed or timed out", *gripper));
   return EXIT_FAILURE;
}
```

The SDK never calls `recoverFromFault()` for you implicitly, because it
releases any grip and sweeps the fingers through their full range —
motion the caller needs to expect. Falling through to `EXIT_FAILURE`
when the result is neither `Activated` nor `AlreadyActive` also
catches a `Timeout` from either call, which a check for `FaultLatched`
alone would miss.

## The `moveTo()` helper's three waits

Sending a `GoTo` command doesn't mean the move is done, or even that
the gripper received it. `moveTo()` waits in three stages, each
catching a different phase of the movement sequence.

<!-- snippet: move_gripper.cpp move-to-three-waits -->
```cpp
if(!Robotiq::waitFor([&] { return gripper.getStatus().positionRequestEcho == *position; }, 1s))
{
   logger.log(Robotiq::Logger::Level::Error, withStatus("the gripper never echoed the position request", gripper));
   return false;
}
// Object detection can lag the echo by a few cycles: give the motion
// a moment to start (returns early once it does). A short move can be
// over before it is ever seen moving, so this one is only advisory.
if(!Robotiq::waitFor([&] { return gripper.getStatus().gripperStatus.objectDetection() == ObjectDetection::Moving; },
                     200ms))
{
   logger.log(Robotiq::Logger::Level::Debug, "no motion seen within 200 ms; it may already be done");
}
if(!Robotiq::waitFor([&] { return motionSettled(gripper); }, 5s))
{
   logger.log(Robotiq::Logger::Level::Error, withStatus("the motion never settled", gripper));
   return false;
}
```

1. **Wait for the position-request echo** (`positionRequestEcho`) —
   confirms the gripper actually received this command, as opposed to
   it being lost or still in flight. A timeout here fails the move.
2. **Wait (briefly) to see `objectDetection() == Moving`** — advisory
   only, and its own timeout is *not* treated as failure. `objectDetection()`
   can lag the echo by a few exchange cycles, and a short move can
   finish before it's ever observed mid-motion — so "never saw it
   moving" doesn't mean anything went wrong.
3. **Wait for `objectDetection() != Moving`** (`motionSettled()`) — the
   actual completion: either the gripper reached the requested
   position, or it stopped early on a detected object. A timeout here
   fails the move.

`command` itself is kept as one persistent `GripperCommand` across
both moves in `main()` (built once via `GripperCommand::defaults()`,
then mutated by each `moveTo()` call) rather than rebuilt from scratch
per move, since only `positionRequest` and the `GoTo` bit actually
change between them.
