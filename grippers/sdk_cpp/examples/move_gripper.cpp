// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! Runtime-mode example — the way to control a gripper with this SDK.
//! The gripper's fingers move (activation sweep, open, close): keep the
//! jaws clear.
//! Usage: move_gripper <port> [baudrate]

#include <cstdlib> // EXIT_SUCCESS / EXIT_FAILURE, main()'s return codes below — unrelated to Gripper itself
#include <memory> // smart pointers: std::unique_ptr to own the Gripper, std::shared_ptr for the Logger it takes
#include <optional> // std::optional from the unit-conversion functions below
#include <iostream> // std::cerr for usage errors
#include <string> // std::stoul (baudrate parsing)

#include <Robotiq/gripper.hpp> // Gripper, GripperCommand/Status
#include <Robotiq/gripper/device_profile.hpp> // DeviceProfile, profiles::k2F85
#include <Robotiq/gripper/stderr_logger.hpp>
#include <Robotiq/gripper/units.hpp>

#include "gripper_events.hpp" // connectGripper(), activateOrRecover(), moveTo(): shared with the other examples

// The handful of SDK types this example touches directly:
using Robotiq::Gripper;
using Robotiq::GripperCommand;
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

   const std::unique_ptr<Gripper> gripper = examples::connectGripper(config);
   if(!gripper)
   {
      return EXIT_FAILURE;
   }

   logger->log(Robotiq::Logger::Level::Info, "Activating...");
   if(!examples::activateOrRecover(*gripper, *logger))
   {
      return EXIT_FAILURE;
   }

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
   if(!examples::moveTo(*gripper, command, k2F85.maxOpening, k2F85, *logger))
   {
      return EXIT_FAILURE;
   }

   logger->log(Robotiq::Logger::Level::Info, "Closing...");
   if(!examples::moveTo(*gripper, command, k2F85.minOpening, k2F85, *logger))
   {
      return EXIT_FAILURE;
   }
   return EXIT_SUCCESS;
}
