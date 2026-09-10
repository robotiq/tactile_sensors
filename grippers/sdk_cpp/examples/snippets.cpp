// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// Doc-only source: every //! [tag] region below backs a \snippet reference
// from a header's \code{.cpp} example (see EXAMPLE_PATH in ../Doxyfile).
// Nothing here is called from main() below — main() only exists so this
// compiles to a program; being compiled by GRIPPERS_BUILD_EXAMPLES, and thus
// checked by every normal build, is the whole point. Concepts already
// demonstrated by quick_start.cpp/move_gripper.cpp are tagged there instead;
// this file is only for the ones with no other home.

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/fake/gripper_factory.hpp>
#include <Robotiq/gripper/stderr_logger.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>

using namespace std::chrono_literals;

void configureConnection()
{
   //! [configure-connection]
   Robotiq::ConnectionConfig config;
   config.serial.port = "/dev/ttyUSB0"; // no default; every other field below is
   config.serial.baudrate = 115200;
   config.serial.timeout = 500ms;
   config.serial.latencyTimerMs = 1;
   config.modbusSlaveAddress = 0x09;
   config.connectionFrequency = 100.0; // Hz
   //! [configure-connection]
}

void commandActionBits()
{
   //! [command-action-bits]
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
   //! [command-action-bits]
}

// Every field statusGripperStatusFields() below reads out, bundled up so
// the function can return them all — the point of the example is the
// nine accessor calls, not this struct.
struct StatusFields
{
   Robotiq::ObjectDetection gOBJ;
   Robotiq::ActivationState gSTA;
   bool gGTO;
   bool gACT;
   Robotiq::ControllerFault kFLT;
   Robotiq::GripperFault gFLT;
   uint8_t gPR;
   uint8_t gPO;
   uint8_t gCU;
};

StatusFields statusGripperStatusFields(Robotiq::Gripper& gripper)
{
   //! [status-gripper-status-fields]
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
   //! [status-gripper-status-fields]
   return {gOBJ, gSTA, gGTO, gACT, kFLT, gFLT, gPR, gPO, gCU};
}

void actionRequestBits()
{
   //! [action-request-bits]
   Robotiq::GripperCommand command = Robotiq::GripperCommand::defaults();
   command.action.set(Robotiq::ActionRequestBit::GoTo); // start moving (same as set(GoTo, true))
   command.action.set(Robotiq::ActionRequestBit::AutoRelease, false); // ...but not an emergency release
   //! [action-request-bits]
}

//! [gripper-status-flags]
bool gripperStatusFlags(Robotiq::Gripper& gripper)
{
   Robotiq::GripperStatus status = gripper.getStatus();
   bool activated = status.gripperStatus.activated(); // gACT flag
   bool goTo = status.gripperStatus.goToEnabled(); // gGTO flag
   if(status.gripperStatus.activationState() == Robotiq::ActivationState::Complete
      && status.gripperStatus.objectDetection() != Robotiq::ObjectDetection::Moving)
   {
      // motion has settled
   }
   return activated && goTo;
}
//! [gripper-status-flags]

//! [motion-settled-example]
void motionSettledExample(Robotiq::Gripper& gripper)
{
   Robotiq::GripperStatus status = gripper.getStatus();
   if(status.gripperStatus.objectDetection() != Robotiq::ObjectDetection::Moving)
   {
      // motion has settled
   }
}
//! [motion-settled-example]

void faultSeverityCheck(Robotiq::Gripper& gripper)
{
   //! [fault-severity-check]
   Robotiq::FaultStatus fault = gripper.getStatus().faultStatus;
   if(Robotiq::severity(fault.gripperFault()) == Robotiq::FaultSeverity::Major)
   {
      Robotiq::recoverFromFault(gripper);
   }
   //! [fault-severity-check]
}

//! [uart-logger]
void uartWrite(std::string_view) {} // stands in for a real UART driver

class UartLogger : public Robotiq::Logger
{
public:
   void log(Level level, std::string_view message) override
   {
      if(level >= Level::Warn)
      {
         uartWrite("!! ");
      }
      uartWrite(message);
   }
};

void useCustomLogger()
{
   auto logger = std::make_shared<UartLogger>();
   Robotiq::ConnectionConfig config;
   config.serial.port = "/dev/ttyUSB0";
   Robotiq::Gripper gripper(config, logger);
}
//! [uart-logger]

void makeFakeGripperUsage()
{
   //! [make-fake-gripper]
   auto gripper = Robotiq::makeFakeGripper(); // no hardware needed
   Robotiq::activate(*gripper);
   //! [make-fake-gripper]
}

//! [wait-with-platform]
bool waitWithPlatform(Robotiq::Gripper& gripper, uint8_t target)
{
   bool settled = Robotiq::waitFor([&] { return gripper.getStatus().positionRequestEcho == target; },
                                   gripper.platform(),
                                   std::chrono::seconds(1));
   return settled;
}
//! [wait-with-platform]

void autoreleaseThenMove(Robotiq::Gripper& gripper)
{
   //! [autorelease-then-move]
   // Build and set an autorelease command
   Robotiq::GripperCommand command = Robotiq::GripperCommand::defaults();
   command.action.set(Robotiq::ActionRequestBit::AutoRelease);
   gripper.setCommand(command);

   // Build and set a command to move the gripper to the position 100
   command.action.set(Robotiq::ActionRequestBit::GoTo);
   command.positionRequest = 100;
   gripper.setCommand(command);
   //! [autorelease-then-move]
}

void autoreleaseThenMoveWithWait(Robotiq::Gripper& gripper)
{
   //! [autorelease-then-move-with-wait]
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
   //! [autorelease-then-move-with-wait]
}

bool waitForMotionSettled(Robotiq::Gripper& gripper)
{
   //! [wait-for-motion-settled]
   bool settled = Robotiq::waitFor(
      [&] { return gripper.getStatus().gripperStatus.objectDetection() != Robotiq::ObjectDetection::Moving; },
      10s);
   //! [wait-for-motion-settled]
   return settled;
}

Robotiq::ActivationResult activateOnly(Robotiq::Gripper& gripper)
{
   //! [activate-only]
   Robotiq::ActivationResult result = Robotiq::activate(gripper);
   //! [activate-only]
   return result;
}

Robotiq::ActivationResult recoverFromFaultOnly(Robotiq::Gripper& gripper)
{
   //! [recover-from-fault-only]
   Robotiq::ActivationResult result = Robotiq::recoverFromFault(gripper);
   //! [recover-from-fault-only]
   return result;
}

void basicLoggerInjection(Robotiq::ConnectionConfig& config)
{
   //! [basic-logger-injection]
   auto logger = std::make_shared<Robotiq::StderrLogger>();
   Robotiq::Gripper gripper(config, logger);
   //! [basic-logger-injection]
}

int main()
{
   return 0;
}
