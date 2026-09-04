// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! Runtime-mode example — the way to control a gripper with this SDK.
//! The gripper's fingers move (activation sweep, open, close): keep the
//! jaws clear.
//! Usage: move_gripper <port> [baudrate]

#include <chrono>
#include <cstdlib>
#include <memory>
#include <iostream>
#include <string>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/stderr_logger.hpp>

using namespace std::chrono_literals;
using Robotiq::ActionRequestBit;
using Robotiq::ActivationResult;
using Robotiq::Gripper;
using Robotiq::GripperCommand;
using Robotiq::ObjectDetection;

namespace {
// Any rate a serial link plausibly runs at. Also what catches a negative:
// stoul("-1") wraps to a huge value rather than throwing.
constexpr unsigned long kMinBaudrate = 1;
constexpr unsigned long kMaxBaudrate = 1000000;

bool motionSettled(Gripper& gripper)
{
   return gripper.getStatus().gripperStatus.objectDetection() != ObjectDetection::Moving;
}

const char* toString(Robotiq::ConnectionState state)
{
   switch(state)
   {
   case Robotiq::ConnectionState::Disconnected:
      return "Disconnected";
   case Robotiq::ConnectionState::Connecting:
      return "Connecting";
   case Robotiq::ConnectionState::Operational:
      return "Operational";
   case Robotiq::ConnectionState::Faulted:
      return "Faulted";
   }
   return "Unrecognized";
}

// One log line: the message, the link state and the decoded status. The
// process image freezes when the link faults, so the status alone can
// look healthy while the gripper is unplugged.
std::string withStatus(std::string message, Gripper& gripper)
{
   const std::string status = Robotiq::toString(gripper.getStatus());
   message += "; link=";
   message += toString(gripper.connectionState());
   message += ' ';
   message += status;
   return message;
}

// Request a position, wait for the gripper to acknowledge the request
// (gPR echo), then wait for the motion to settle. False if either wait
// timed out — a wait result is never worth dropping.
bool moveTo(Gripper& gripper, GripperCommand& command, uint8_t position, Robotiq::Logger& logger)
{
   command.positionRequest = position;
   command.action.set(ActionRequestBit::GoTo, true); // execute the move
   gripper.setCommand(command);
   logger.log(Robotiq::Logger::Level::Debug, "sending: " + Robotiq::toString(command));
   if(!Robotiq::waitFor([&] { return gripper.getStatus().positionRequestEcho == position; }, 1s))
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
   logger.log(Robotiq::Logger::Level::Info, withStatus("settled", gripper));
   return true;
}
} // namespace

int main(int argc, char* argv[])
{
   if(argc < 2)
   {
      std::cerr << "Usage: " << argv[0] << " <port> [baudrate]\n";
      return EXIT_FAILURE;
   }

   Robotiq::ConnectionConfig config;
   config.serial.port = argv[1];
   if(argc > 2)
   {
      try
      {
         const unsigned long parsed = std::stoul(argv[2]);
         if(parsed < kMinBaudrate || parsed > kMaxBaudrate)
         {
            throw std::out_of_range("baudrate outside the supported range");
         }
         config.serial.baudrate = static_cast<uint32_t>(parsed);
      }
      catch(const std::exception&)
      {
         std::cerr << "Invalid baudrate '" << argv[2] << "'\n";
         return EXIT_FAILURE;
      }
   }

   // The SDK logs through an injectable sink; naming the instances
   // tells library and application lines apart in the shared stream.
   // (A real integration gets the same separation from its injected
   // adapter, e.g. a named rclcpp logger.)
   auto logger = std::make_shared<Robotiq::StderrLogger>("example");

   std::unique_ptr<Gripper> gripper;
   try
   {
      // Opens the port and starts exchanging.
      gripper = std::make_unique<Gripper>(config, std::make_shared<Robotiq::StderrLogger>("robotiq"));
   }
   catch(const std::exception& ex)
   {
      std::cerr << "Error: " << ex.what() << "\n\n"
                << "Could not open a gripper on '" << argv[1] << "'. Check that:\n"
                << "  - the gripper is connected and powered;\n"
                << "  - the port name is correct (Linux /dev/ttyUSB0, macOS /dev/tty.usbserial-*, Windows COM3);\n"
                << "  - you have permission to use it (Linux: join the 'dialout' group).\n";
      return EXIT_FAILURE;
   }

   logger->log(Robotiq::Logger::Level::Info, "Activating...");
   ActivationResult activation = Robotiq::activate(*gripper);
   if(activation == ActivationResult::FaultLatched)
   {
      // Recovery releases any grip and sweeps the fingers, so the SDK
      // never runs it implicitly; this example has no part to drop.
      logger->log(Robotiq::Logger::Level::Warn, "fault latched; recovering (the fingers will move)");
      activation = Robotiq::recoverFromFault(*gripper);
   }
   if(activation != ActivationResult::Activated && activation != ActivationResult::AlreadyActive)
   {
      logger->log(Robotiq::Logger::Level::Error, withStatus("activation failed or timed out", *gripper));
      return EXIT_FAILURE;
   }
   logger->log(Robotiq::Logger::Level::Info, withStatus("activated", *gripper));

   // Keep one command block and update it before each send: it is
   // persistent state, not rebuilt per move.
   GripperCommand command = GripperCommand::defaults(); // GoTo added by moveTo

   logger->log(Robotiq::Logger::Level::Info, "Opening...");
   if(!moveTo(*gripper, command, 0, *logger))
   {
      return EXIT_FAILURE;
   }

   logger->log(Robotiq::Logger::Level::Info, "Closing...");
   if(!moveTo(*gripper, command, 255, *logger))
   {
      return EXIT_FAILURE;
   }
   return EXIT_SUCCESS;
}
