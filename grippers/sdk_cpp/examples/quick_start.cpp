//! [qs-includes]
// Import gripper C++ driver
#include <Robotiq/gripper.hpp>

// Import utilities libraries
#include <iostream>
#include <chrono>
using namespace std::chrono_literals;
//! [qs-includes]

int main(int argc, char* argv[])
{
   // 1- Create the connection config
   //! [qs-config]
   if(argc < 2)
   {
      std::cerr << "Usage: quick_start <port>\n";
      return 1;
   }
   Robotiq::ConnectionConfig config;
   config.serial.port = argv[1]; // e.g. "COM4" on Windows, "/dev/ttyUSB0" on Linux, "/dev/tty.usbserial-XXXX" on macOS
   //! [qs-config]

   // 2- Create the gripper object
   //! [qs-create-gripper]
   Robotiq::Gripper gripper(config);
   //! [qs-create-gripper]

   // 3- Activate the gripper
   //! [qs-activate]
   Robotiq::activate(gripper);
   //! [qs-activate]

   // 4- Create a command to move the gripper
   //! [qs-create-command]
   Robotiq::GripperCommand command = Robotiq::GripperCommand::defaults();
   command.action.set(Robotiq::ActionRequestBit::GoTo);
   command.positionRequest = 100;
   command.speed = 255;
   command.force = 255;
   //! [qs-create-command]

   // 5- Set command
   //! [qs-send-command]
   gripper.setCommand(command);
   //! [qs-send-command]

   //! [qs-wait]
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
   //! [qs-wait]

   //! [qs-status]
   // 9- retrieve status
   uint8_t currentPosition = gripper.getStatus().position;

   // Print retrieved status
   std::cout << "Current position : " << static_cast<int>(currentPosition) << std::endl;
   //! [qs-status]

   // End of the program
   return 0;
}
