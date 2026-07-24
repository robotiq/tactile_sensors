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

using namespace std::chrono_literals;
using Robotiq::ActionRequestBit;
using Robotiq::ActivationResult;
using Robotiq::Gripper;
using Robotiq::GripperCommand;
using Robotiq::ObjectDetection;

namespace {
bool motionSettled(Gripper& gripper)
{
   return gripper.getStatus().gripperStatus.objectDetection() != ObjectDetection::Moving;
}

// Request a position, wait for the gripper to acknowledge the request
// (gPR echo), then wait for the motion to settle.
void moveTo(Gripper& gripper, GripperCommand& command, uint8_t position, Robotiq::Logger& logger)
{
   command.positionRequest = position;
   command.action.set(ActionRequestBit::GoTo, true); // execute the move
   gripper.setCommand(command);
   Robotiq::waitFor([&] { return gripper.getStatus().positionRequestEcho == position; }, 1s);
   // Object detection can lag the echo by a few cycles: give the motion
   // a moment to start (returns early once it does).
   Robotiq::waitFor([&] { return gripper.getStatus().gripperStatus.objectDetection() == ObjectDetection::Moving; },
                    200ms);
   Robotiq::waitFor([&] { return motionSettled(gripper); }, 5s);
   logger.log(Robotiq::Logger::Level::Info, "Position: " + std::to_string(gripper.getStatus().position) + "/255");
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
         config.serial.baudrate = static_cast<uint32_t>(std::stoul(argv[2]));
      }
      catch(const std::exception&)
      {
         std::cerr << "Invalid baudrate '" << argv[2] << "'\n";
         return EXIT_FAILURE;
      }
   }

   // The SDK logs through an injectable sink; the example shares it
   // for its own narration so all lines land on one ordered stream.
   auto logger = std::make_shared<Robotiq::StderrLogger>();

   std::unique_ptr<Gripper> gripper;
   try
   {
      gripper = std::make_unique<Gripper>(config, logger); // opens and starts exchanging
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
      logger->log(Robotiq::Logger::Level::Error, "activation failed or timed out");
      return EXIT_FAILURE;
   }

   // Keep one command block and update it before each send: it is
   // persistent state, not rebuilt per move.
   GripperCommand command = GripperCommand::defaults(); // GoTo added by moveTo

   logger->log(Robotiq::Logger::Level::Info, "Opening...");
   moveTo(*gripper, command, 0, *logger);

   logger->log(Robotiq::Logger::Level::Info, "Closing...");
   moveTo(*gripper, command, 255, *logger);
   return EXIT_SUCCESS;
}
