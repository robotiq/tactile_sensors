// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! Runtime-mode example — the way to control a gripper with this SDK.
//! The gripper's fingers move (activation sweep, open, close): keep the
//! jaws clear.
//! Usage: move_gripper <port> [baudrate]

#include <chrono> // duration literals for the waitFor() timeouts below (1s, 200ms, ...)
#include <cmath> // std::lround for the settled-position log line
#include <cstdlib> // EXIT_SUCCESS / EXIT_FAILURE, main()'s return codes below — unrelated to Gripper itself
#include <memory> // smart pointers: std::unique_ptr to own the Gripper, std::shared_ptr for the Logger it takes
#include <optional> // std::optional from the unit-conversion functions below
#include <iostream> // std::cerr for usage/connection error messages
#include <string> // std::string (withStatus()'s return type), std::stoul (baudrate parsing)

#include <Robotiq/gripper.hpp> // Gripper, GripperCommand/Status, activate(), recoverFromFault()
#include <Robotiq/gripper/device_profile.hpp> // DeviceProfile, profiles::k2F85
#include <Robotiq/gripper/stderr_logger.hpp>
#include <Robotiq/gripper/units.hpp>

using namespace std::chrono_literals; // enables the 1s / 200ms / 5s literals below

// The handful of SDK types this example touches directly:
using Robotiq::ActionRequestBit;
using Robotiq::ActivationResult;
using Robotiq::Gripper;
using Robotiq::GripperCommand;
using Robotiq::ObjectDetection;
using Robotiq::profiles::k2F85;

namespace {
// Any rate a serial link plausibly runs at. Also what catches a negative:
// stoul("-1") wraps to a huge value rather than throwing.
//! [baudrate-bounds]
constexpr unsigned long kMinBaudrate = 1;
constexpr unsigned long kMaxBaudrate = 1000000;
//! [baudrate-bounds]

constexpr double kSpeed = 0.150; // m/s, the 2F-85's full scale
constexpr double kEffort = 1.0; // maximum grip effort

//! \brief Whether the fingers have stopped moving.
//!
//! \param gripper The gripper to read status from.
//! \return true once motion has settled (stopped on an object, or reached
//!         the requested position); false while still moving.
bool motionSettled(Gripper& gripper)
{
   return gripper.getStatus().gripperStatus.objectDetection() != ObjectDetection::Moving;
}

//! \brief Return the input message associated with the gripper's current
//!        status, for logging and error reporting.
//!
//! \param message The message to prepend before the appended status.
//! \param gripper The gripper to read the connection state and status from.
//! \return \p message followed by "; link=<state> <decoded status>".
std::string withStatus(std::string message, Gripper& gripper)
{
   const std::string status = Robotiq::toString(gripper.getStatus());
   message += "; link=";
   message += Robotiq::toString(gripper.connectionState());
   message += ' ';
   message += status;
   return message;
}

//! \brief Send the gripper to an opening and block until it gets there.
//!
//! \param gripper The gripper to command.
//! \param command The persistent command block; positionRequest and the
//!        GoTo bit are set on it before sending.
//! \param openingMetres Target jaw opening, in metres (k2F85.minOpening = fully closed,
//!        k2F85.maxOpening = fully open).
//! \param logger Where to narrate progress and report failures.
//! \return true once the gripper echoed the request and motion settled;
//!         false if either wait timed out.
bool moveTo(Gripper& gripper, GripperCommand& command, double openingMetres, Robotiq::Logger& logger)
{
   //! [opening-optional-check]
   const std::optional<uint8_t> position = Robotiq::units::openingToRegister(openingMetres, k2F85);
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
   const std::optional<double> opening = Robotiq::units::openingFromRegister(gripper.getStatus().position, k2F85);
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
} // namespace

//! \brief Connect, activate, open, then close — see the file header comment.
//! \param argc Argument count; expects 2 or 3 (program name, port, [baudrate]).
//! \param argv argv[1] is the serial port; argv[2], if given, is the baudrate.
//! \return EXIT_SUCCESS on a full connect/activate/open/close cycle;
//!         EXIT_FAILURE on a bad argument, a connection error, or a timeout.
int main(int argc, char* argv[])
{
   //! [argument-handling]
   if(argc < 2)
   {
      std::cerr << "Usage: " << argv[0] << " <port> [baudrate]\n";
      return EXIT_FAILURE;
   }
   //! [argument-handling]

   Robotiq::ConnectionConfig config;
   config.serial.port = argv[1];
   if(argc > 2)
   {
      try
      {
         //! [baudrate-parse-and-check]
         const unsigned long parsed = std::stoul(argv[2]);
         if(parsed < kMinBaudrate || parsed > kMaxBaudrate)
         {
            throw std::out_of_range("baudrate outside the supported range");
         }
         //! [baudrate-parse-and-check]
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
   //! [logger-example-name]
   auto logger = std::make_shared<Robotiq::StderrLogger>("example");
   //! [logger-example-name]

   std::unique_ptr<Gripper> gripper;
   try
   {
      //! [logger-robotiq-name]
      gripper =
         std::make_unique<Gripper>(config,
                                   std::make_shared<Robotiq::StderrLogger>("robotiq")); // opens and starts exchanging
      //! [logger-robotiq-name]
   }
   //! [connection-error-checklist]
   catch(const std::exception& ex)
   {
      std::cerr << "Error: " << ex.what() << "\n\n"
                << "Could not open a gripper on '" << argv[1] << "'. Check that:\n"
                << "  - the gripper is connected and powered;\n"
                << "  - the port name is correct (Linux /dev/ttyUSB0, macOS /dev/tty.usbserial-*, Windows COM3);\n"
                << "  - you have permission to use it (Linux: join the 'dialout' group).\n";
      return EXIT_FAILURE;
   }
   //! [connection-error-checklist]

   logger->log(Robotiq::Logger::Level::Info, "Activating...");
   //! [activation-recovery]
   ActivationResult activation = Robotiq::activate(*gripper);
   if(activation == ActivationResult::FaultLatched)
   {
      // Recovery releases any grip and sweeps the fingers, so the SDK
      // never runs it implicitly; this example has no part to drop.
      logger->log(Robotiq::Logger::Level::Warn, "fault latched; recovering (the fingers will move)");
      activation = Robotiq::recoverFromFault(*gripper);
   }
   //! [activation-recovery]
   //! [activation-final-check]
   if(activation != ActivationResult::Activated && activation != ActivationResult::AlreadyActive)
   {
      logger->log(Robotiq::Logger::Level::Error, withStatus("activation failed or timed out", *gripper));
      return EXIT_FAILURE;
   }
   //! [activation-final-check]
   logger->log(Robotiq::Logger::Level::Info, withStatus("activated", *gripper));

   // Keep one command block and update it before each send: it is
   // persistent state, not rebuilt per move.
   GripperCommand command = GripperCommand::defaults(); // GoTo added by moveTo
   //! [speed-optional-check]
   const std::optional<uint8_t> speed = Robotiq::units::speedToRegister(kSpeed, k2F85);
   const std::optional<uint8_t> force = Robotiq::units::effortToRegister(kEffort);
   if(!speed || !force)
   {
      logger->log(Robotiq::Logger::Level::Error, "the requested speed or effort has no register value");
      return EXIT_FAILURE;
   }
   command.speed = *speed;
   command.force = *force;
   //! [speed-optional-check]

   logger->log(Robotiq::Logger::Level::Info, "Opening...");
   if(!moveTo(*gripper, command, k2F85.maxOpening, *logger))
   {
      return EXIT_FAILURE;
   }

   logger->log(Robotiq::Logger::Level::Info, "Closing...");
   if(!moveTo(*gripper, command, k2F85.minOpening, *logger))
   {
      return EXIT_FAILURE;
   }
   return EXIT_SUCCESS;
}
