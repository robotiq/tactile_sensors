// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "gripper_events.hpp"

#include <chrono> // duration literals for the waitFor() timeouts below (1s, 200ms, ...)
#include <cmath> // std::lround for the settled-position log line
#include <iostream> // std::cerr for the connection error checklist
#include <optional> // std::optional from the unit-conversion functions below
#include <string>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/device_profile.hpp>
#include <Robotiq/gripper/stderr_logger.hpp>
#include <Robotiq/gripper/units.hpp>

using namespace std::chrono_literals; // enables the 1s / 200ms / 5s literals below

// The handful of SDK types this file touches directly:
using Robotiq::ActionRequestBit;
using Robotiq::ActivationResult;
using Robotiq::DeviceProfile;
using Robotiq::Gripper;
using Robotiq::GripperCommand;
using Robotiq::ObjectDetection;

namespace examples {

std::string withStatus(std::string message, Gripper& gripper)
{
   const std::string status = Robotiq::toString(gripper.getStatus());
   message += "; link=";
   message += Robotiq::toString(gripper.connectionState());
   message += ' ';
   message += status;
   return message;
}

bool motionSettled(Gripper& gripper)
{
   return gripper.getStatus().gripperStatus.objectDetection() != ObjectDetection::Moving;
}

std::unique_ptr<Gripper> connectGripper(const Robotiq::ConnectionConfig& config)
{
   try
   {
      //! [logger-robotiq-name]
      return std::make_unique<Gripper>(
         config,
         std::make_shared<Robotiq::StderrLogger>("robotiq")); // opens and starts exchanging
      //! [logger-robotiq-name]
   }
   //! [connection-error-checklist]
   catch(const std::exception& ex)
   {
      std::cerr << "Error: " << ex.what() << "\n\n"
                << "Could not open a gripper on '" << config.serial.port << "'. Check that:\n"
                << "  - the gripper is connected and powered;\n"
                << "  - the port name is correct (Linux /dev/ttyUSB0, macOS /dev/tty.usbserial-*, Windows COM3);\n"
                << "  - you have permission to use it (Linux: join the 'dialout' group).\n";
      return nullptr;
   }
   //! [connection-error-checklist]
}

bool activateOrRecover(Gripper& gripper, Robotiq::Logger& logger)
{
   //! [activation-recovery]
   ActivationResult activation = Robotiq::activate(gripper);
   if(activation == ActivationResult::FaultLatched)
   {
      // Recovery releases any grip and sweeps the fingers, so the SDK
      // never runs it implicitly; the examples have no part to drop.
      logger.log(Robotiq::Logger::Level::Warn, "fault latched; recovering (the fingers will move)");
      activation = Robotiq::recoverFromFault(gripper);
   }
   //! [activation-recovery]
   //! [activation-final-check]
   if(activation != ActivationResult::Activated && activation != ActivationResult::AlreadyActive)
   {
      logger.log(Robotiq::Logger::Level::Error, withStatus("activation failed or timed out", gripper));
      return false;
   }
   //! [activation-final-check]
   logger.log(Robotiq::Logger::Level::Info, withStatus("activated", gripper));
   return true;
}

bool moveTo(Gripper& gripper,
            GripperCommand& command,
            double openingMetres,
            const DeviceProfile& profile,
            Robotiq::Logger& logger)
{
   //! [opening-optional-check]
   const std::optional<uint8_t> position = Robotiq::units::openingToRegister(openingMetres, profile);
   if(!position)
   {
      logger.log(Robotiq::Logger::Level::Error, "the requested opening has no register value");
      return false;
   }
   //! [opening-optional-check]
   command.positionRequest = *position;
   command.action.set(ActionRequestBit::GoTo, true); // execute the move
   gripper.setCommand(command);
   logger.log(Robotiq::Logger::Level::Debug, "sending: " + Robotiq::toString(command));
   //! [move-to-three-waits]
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
   //! [move-to-three-waits]
   //! [opening-from-register-optional-check]
   const std::optional<double> opening = Robotiq::units::openingFromRegister(gripper.getStatus().position, profile);
   if(!opening)
   {
      logger.log(Robotiq::Logger::Level::Error,
                 withStatus("the settled position has no opening in this profile", gripper));
      return false;
   }
   //! [opening-from-register-optional-check]
   logger.log(Robotiq::Logger::Level::Info,
              withStatus("settled at " + std::to_string(std::lround(*opening * 1000.0)) + " mm", gripper));
   return true;
}

} // namespace examples
