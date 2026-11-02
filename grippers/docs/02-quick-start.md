# Quick start

This is a minimalist introduction showing how to control the gripper using the C++ driver.

[![Quick start walkthrough](https://img.youtube.com/vi/RSUoc7jSA7E/0.jpg)](https://youtu.be/RSUoc7jSA7E)

Refer to API documentation to get more details.

> **Note :**
> This example does not handle potential errors. Refer to move_gripper.cpp for a robust example.

> **Note :**
> The code of this example is in the file quick_start.cpp.

## Import dependencies

<!-- snippet: quick_start.cpp qs-includes -->
```cpp
// Import gripper C++ driver
#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/wait.hpp>

// Import utilities libraries
#include <iostream>
#include <chrono>
using namespace std::chrono_literals;
```

## Write the main program

Everything from here on goes inside `int main(int argc, char* argv[]) { ... }` in quick_start.cpp.

### Create a connection configuration
Create a ConnectionConfig object and specify which port the gripper is connected to.
The port is read from the program's first command-line argument.

To find which port your gripper is actually connected to:
- **Windows**: open Device Manager → **Ports (COM & LPT)**. The gripper
  shows up as an FTDI USB Serial Port; note its `COMx` number.
- **Linux**: run `ls /dev/ttyUSB*` — the gripper is typically `/dev/ttyUSB0`
  (or the newest device that appears after plugging it in).
- **macOS**: run `ls /dev/tty.usbserial-*`.

<!-- snippet: quick_start.cpp qs-config -->
```cpp
if(argc < 2)
{
   std::cerr << "Usage: quick_start <port>\n";
   return 1;
}
Robotiq::ConnectionConfig config;
config.serial.port = argv[1]; // e.g. "COM4" on Windows, "/dev/ttyUSB0" on Linux, "/dev/tty.usbserial-XXXX" on macOS
```

Run the built binary with your port. Building this repository directly,
as [Environment setup](01-environment-setup.md#try-it-on-your-gripper)
describes, puts it here:

```sh
./build/examples/quick_start /dev/ttyUSB0
```

```powershell
.\build\examples\Release\quick_start.exe COM4
```

### Create a gripper object
Create a gripper object using the previously created connection configuration.

<!-- snippet: quick_start.cpp qs-create-gripper -->
```cpp
Robotiq::Gripper gripper(config);
```

### Activate the gripper
Gripper activation is the first action to perform before being able to use the
gripper. The C++ driver provides a function to perform gripper activation.

<!-- snippet: quick_start.cpp qs-activate -->
```cpp
Robotiq::activate(gripper);
```

> **Note :**
> If the gripper is already activated, the activate function does nothing. To force the activation process the gripper activate (rACT) bit has to be set to false or the gripper power has to be removed.
>
> This is different from `recoverFromFault()`, which also reactivates the gripper immediately (running the calibration sweep) as part of the same call — use the approach above instead if you want the gripper to stay deactivated until you call `activate()` yourself.

### Create a command

To control the gripper you have to write a command with appropriate parameters and send it.

The command is initially built from a default command, as shown below.
The GoTo bit of the action register has to be set to 1 so that the gripper moves to the position written in its position register.

<!-- snippet: quick_start.cpp qs-create-command -->
```cpp
Robotiq::GripperCommand command = Robotiq::GripperCommand::defaults();
command.action.set(Robotiq::ActionRequestBit::GoTo);
command.positionRequest = 100;
command.speed = 255;
command.force = 255;
```

### Send the command
Once the command is prepared, `setCommand` hands it to the exchange cycle and returns at once; the cycle carries it to the gripper on its next exchange. A program that needs to know when that happened uses `setCommandAndWaitForExchange` instead, which returns the exchange that carried it; see [Waiting for a condition](03-how-it-works.md#waiting-for-a-condition).

<!-- snippet: quick_start.cpp qs-send-command -->
```cpp
gripper.setCommand(command);
```

### Wait for the action to be completed
Two named waits follow the motion. `waitForObjectDetection` waits (briefly) for the gripper to report `Moving`. If the gripper was already at the requested position it never does; this wait then simply times out after 200 ms and the program moves on. `waitForMotionEnd` then waits for the fingers to stop, at the requested position or on an object.

Each wait wakes on the exchange that shows the condition and returns it. For a condition no named wait covers, `waitFor(gripper, predicate, timeout)` takes a predicate; see [Waiting for a condition](03-how-it-works.md#waiting-for-a-condition).

<!-- snippet: quick_start.cpp qs-wait -->
```cpp
// 6- Wait for the gripper to start moving
Robotiq::waitForObjectDetection(gripper, Robotiq::ObjectDetection::Moving, 200ms);

// 7- Wait for the gripper to stop
Robotiq::waitForMotionEnd(gripper, 5s);
```

### Retrieve gripper status

The status of the gripper can be retrieved with the getStatus function. Here is an example where we retrieve and print the current position of the gripper.

<!-- snippet: quick_start.cpp qs-status -->
```cpp
// 8- retrieve status
uint8_t currentPosition = gripper.getStatus().position;

// Print retrieved status
std::cout << "Current position : " << static_cast<int>(currentPosition) << std::endl;
```
