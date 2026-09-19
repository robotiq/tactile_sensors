// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! Logging example — a recorder thread writes one CSV row per exchange
//! cycle, in step with the cycle through waitForExchangeCount().
//! Meanwhile the main thread activates the gripper, opens it at full
//! speed, then moves it to a 30 mm opening at minimum speed.
//!
//! Usage: exchange_log <port> [file.csv]

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/device_profile.hpp>
#include <Robotiq/gripper/stderr_logger.hpp>
#include <Robotiq/gripper/units.hpp>

#include "gripper_events.hpp"

using namespace Robotiq;

namespace {
constexpr double kTargetOpening = 0.030; // m
constexpr double kExchangeRate = 200.0; // Hz
constexpr auto kCycleTimeout = std::chrono::milliseconds(500);
constexpr const char* kDefaultFile = "exchange_log.csv";

// The manual's register names, so the file reads with it open.
constexpr const char* kMetadataColumns = "time_s,exchange_count";
constexpr const char* kCommandColumns = "rACT,rGTO,rATR,rARD,rPR,rSP,rFR";
constexpr const char* kStatusColumns = "gACT,gGTO,gSTA,gOBJ,gFLT,gPR,gPO,gCU";

void writeHeader(std::ostream& csv)
{
   csv << kMetadataColumns << ',' //
       << kCommandColumns << ',' //
       << kStatusColumns << '\n';
}

// The fields in kMetadataColumns order, the time relative to \p start.
void writeMetadata(std::ostream& csv, std::chrono::steady_clock::time_point start, const ExchangeMetadata& metadata)
{
   csv << std::chrono::duration<double>(metadata.timestamp - start).count() << ',' //
       << metadata.exchangeCount;
}

// The block's fields in kCommandColumns order.
void writeCommand(std::ostream& csv, const GripperCommand& command)
{
   csv << command.action.get(ActionRequestBit::Activate) << ',' //
       << command.action.get(ActionRequestBit::GoTo) << ',' //
       << command.action.get(ActionRequestBit::AutoRelease) << ',' //
       << command.action.get(ActionRequestBit::AutoReleaseOpenDirection) << ',' //
       << +command.positionRequest << ',' //
       << +command.speed << ',' //
       << +command.force;
}

// The block's fields in kStatusColumns order.
void writeStatus(std::ostream& csv, const GripperStatus& status)
{
   const GripperStatusFlags flags = status.gripperStatus;
   csv << flags.activated() << ',' //
       << flags.goToEnabled() << ',' //
       << +static_cast<uint8_t>(flags.activationState()) << ',' //
       << +static_cast<uint8_t>(flags.objectDetection()) << ',' //
       << +status.faultStatus.raw() << ',' //
       << +status.positionRequestEcho << ',' //
       << +status.position << ',' //
       << +status.current;
}

void writeRow(std::ostream& csv, std::chrono::steady_clock::time_point start, const StampedExchange& exchange)
{
   writeMetadata(csv, start, exchange.metadata);
   csv << ',';
   writeCommand(csv, exchange.command);
   csv << ',';
   writeStatus(csv, exchange.status);
   csv << '\n';
}

class Recorder
{
public:
   Recorder(const Gripper& gripper, std::ostream& csv)
      : _gripper(gripper)
      , _csv(csv)
      , _thread([this] { run(); })
   {
   }

   ~Recorder() { stop(); }

   // \return The rows written.
   uint64_t stop()
   {
      _stop = true;
      if(_thread.joinable())
      {
         _thread.join();
      }
      return _rows;
   }

private:
   void run()
   {
      const StampedExchange first = _gripper.getMostRecentStampedExchange();
      const auto start = first.metadata.timestamp;
      uint64_t written = first.metadata.exchangeCount;
      writeHeader(_csv);
      while(!_stop)
      {
         //! [sync-loop]
         const std::optional<StampedExchange> exchange = _gripper.waitForExchangeCount(written + 1, kCycleTimeout);
         if(!exchange)
         {
            continue; // a stalled link: nothing landed, so nothing to log
         }
         writeRow(_csv, start, *exchange);
         written = exchange->metadata.exchangeCount;
         //! [sync-loop]
         ++_rows;
      }
   }

   const Gripper& _gripper;
   std::ostream& _csv;
   std::atomic<bool> _stop{false};
   uint64_t _rows = 0;
   std::thread _thread;
};

bool setSpeed(GripperCommand& command, double metresPerSecond, Logger& logger)
{
   const std::optional<uint8_t> speed = units::speedToRegister(metresPerSecond, profiles::k2F85);
   if(!speed)
   {
      logger.log(Logger::Level::Error, "the requested speed has no register value");
      return false;
   }
   command.speed = *speed;
   return true;
}
} // namespace

int main(int argc, char* argv[])
{
   if(argc < 2)
   {
      std::cerr << "Usage: " << argv[0] << " <port> [file.csv]\n";
      return EXIT_FAILURE;
   }
   const std::string file = argc > 2 ? argv[2] : kDefaultFile;

   ConnectionConfig config;
   config.serial.port = argv[1];
   config.connectionFrequency = kExchangeRate;

   auto logger = std::make_shared<StderrLogger>("example");

   std::ofstream csv(file);
   if(!csv)
   {
      std::cerr << "Error: could not open '" << file << "' for writing\n";
      return EXIT_FAILURE;
   }

   const std::unique_ptr<Gripper> gripper = examples::connectGripper(config);
   if(!gripper)
   {
      return EXIT_FAILURE;
   }

   Recorder recorder(*gripper, csv);

   logger->log(Logger::Level::Info, "Activating...");
   if(!examples::activateOrRecover(*gripper, *logger))
   {
      return EXIT_FAILURE;
   }

   GripperCommand command = GripperCommand::defaults();
   logger->log(Logger::Level::Info, "Opening at full speed...");
   bool ok = setSpeed(command, profiles::k2F85.maxSpeed, *logger)
          && examples::moveTo(*gripper, command, profiles::k2F85.maxOpening, profiles::k2F85, *logger);
   if(ok)
   {
      logger->log(Logger::Level::Info, "Moving to 30 mm at minimum speed...");
      ok = setSpeed(command, profiles::k2F85.minSpeed, *logger)
        && examples::moveTo(*gripper, command, kTargetOpening, profiles::k2F85, *logger);
   }

   logger->log(Logger::Level::Info, std::to_string(recorder.stop()) + " rows written to " + file);
   return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
