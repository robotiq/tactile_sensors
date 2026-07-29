// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/gripper.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <utility>

#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/driver_exception.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/throttle.hpp>
#include <Robotiq/detail/default_serial.hpp>
#include <Robotiq/detail/gripper_modbus_client.hpp>
#include <Robotiq/detail/serial.hpp>

namespace Robotiq {
using detail::DefaultSerial;
using detail::Serial;
namespace {
//! Consecutive exchange failures before state() degrades to Faulted.
constexpr uint64_t kFaultThreshold = 3;
//! Initial status-read attempts before construction fails.
constexpr uint64_t kInitialReadAttempts = 3;
} // namespace

struct Gripper::Impl
{
   std::shared_ptr<Logger> logger;
   detail::GripperModbusClient client;
   Throttle failureLogThrottle{std::chrono::milliseconds(1000)};
   std::chrono::microseconds period;

   mutable std::mutex imageMutex;
   GripperCommand command{};
   GripperStatus status{};

   std::atomic<ConnectionState> state{ConnectionState::Connecting};
   std::atomic<bool> running{false};
   std::atomic<uint64_t> consecutiveFailures{0};
   std::thread exchangeThread;

   Impl(std::unique_ptr<Serial> serial,
        uint8_t slaveAddress,
        std::chrono::microseconds exchangePeriod,
        std::shared_ptr<Logger> log)
      : logger(log ? std::move(log) : makeDefaultLogger())
      , client(std::move(serial), slaveAddress, logger)
      , period(exchangePeriod)
   {
   }

   // RAII: an Impl can never outlive its exchange thread.
   ~Impl() { stop(); }

   // Initialize the command image using the status echoes. Speed and force
   // have no echoes; they are initialized at maximum values, matching the
   // urcap driver behavior.
   void initializeImage()
   {
      GripperStatus fresh;
      for(uint64_t attempt = 1;; ++attempt)
      {
         try
         {
            fresh = client.readStatus();
            break;
         }
         catch(const std::exception& ex)
         {
            if(attempt >= kInitialReadAttempts)
            {
               throw DriverException("no gripper answered the initial status read (is it powered, "
                                     "and the Modbus slave address correct?) — last attempt: "
                                     + std::string(ex.what()));
            }
         }
      }

      const std::lock_guard<std::mutex> lock(imageMutex);
      status = fresh;
      command = GripperCommand::defaults();
      command.action.set(ActionRequestBit::Activate, fresh.gripperStatus.activated());
      command.action.set(ActionRequestBit::GoTo, fresh.gripperStatus.goToEnabled());
      command.positionRequest = fresh.positionRequestEcho;
      state.store(ConnectionState::Operational);
   }

   void exchangeOnce()
   {
      GripperCommand commandCopy;
      {
         const std::lock_guard<std::mutex> lock(imageMutex);
         commandCopy = command;
      }

      GripperStatus freshStatus;
      try
      {
         freshStatus = client.exchange(commandCopy);
      }
      catch(...)
      {
         if(consecutiveFailures.fetch_add(1) + 1 >= kFaultThreshold)
         {
            state.store(ConnectionState::Faulted);
         }
         throw;
      }

      {
         const std::lock_guard<std::mutex> lock(imageMutex);
         status = freshStatus;
      }
      consecutiveFailures.store(0);
      state.store(ConnectionState::Operational);
   }

   void start()
   {
      running.store(true);
      exchangeThread = std::thread([this] {
         auto nextCycle = std::chrono::steady_clock::now();
         while(running.load())
         {
            try
            {
               exchangeOnce();
            }
            catch(const std::exception& ex)
            {
               failureLogThrottle.executeIfAllowed(
                  [&] { logger->log(Logger::Level::Warn, std::string("exchange cycle failed: ") + ex.what()); });
            }
            catch(...)
            {
               failureLogThrottle.executeIfAllowed(
                  [&] { logger->log(Logger::Level::Warn, "exchange cycle failed: unknown exception"); });
            }
            // Overrun cycles (e.g. timeouts during a fault) must not
            // accumulate a backlog that bursts exchanges on recovery.
            nextCycle = std::max(nextCycle + period, std::chrono::steady_clock::now());
            std::this_thread::sleep_until(nextCycle);
         }
      });
   }

   void stop() noexcept
   {
      running.store(false);
      if(exchangeThread.joinable())
      {
         exchangeThread.join();
      }
   }
};

namespace {
std::unique_ptr<Serial> makeSerial(const ConnectionConfig& config, const std::shared_ptr<Logger>& logger)
{
   return std::make_unique<DefaultSerial>(config.serial, logger);
}

// Exchange frequency (Hz) to cycle period; 0 Hz or less means free-run,
// a zero period the exchange loop paces past immediately.
std::chrono::microseconds periodFromFrequency(double hz)
{
   if(hz <= 0.0)
   {
      return std::chrono::microseconds{0};
   }
   return std::chrono::microseconds{std::llround(1000000.0 / hz)};
}
} // namespace

Gripper::Gripper(const ConnectionConfig& config, std::shared_ptr<Logger> logger)
   : Gripper(makeSerial(config, logger),
             config.modbusSlaveAddress,
             periodFromFrequency(config.connectionFrequency),
             logger)
{
}

Gripper::Gripper(std::unique_ptr<detail::Serial> serial,
                 uint8_t slaveAddress,
                 std::chrono::microseconds exchangePeriod,
                 std::shared_ptr<Logger> logger)
   : _impl(std::make_unique<Impl>(std::move(serial), slaveAddress, exchangePeriod, std::move(logger)))
{
   _impl->initializeImage();
   _impl->start();
}

Gripper::~Gripper() = default;

void Gripper::setCommand(const GripperCommand& command)
{
   const std::lock_guard<std::mutex> lock(_impl->imageMutex);
   _impl->command = command;
}

GripperCommand Gripper::getCommand() const
{
   const std::lock_guard<std::mutex> lock(_impl->imageMutex);
   return _impl->command;
}

GripperStatus Gripper::getStatus() const
{
   const std::lock_guard<std::mutex> lock(_impl->imageMutex);
   return _impl->status;
}

ConnectionState Gripper::connectionState() const
{
   return _impl->state.load();
}

namespace {
// The exchange cycle must be delivering fresh status before a procedure
// can judge the gripper: Faulted here is link health, which no command
// can fix. Gripper faults (gFLT) are the callers' business.
bool waitOperational(const Gripper& gripper, std::chrono::steady_clock::time_point deadline)
{
   return waitUntil([&] { return gripper.connectionState() == ConnectionState::Operational; }, deadline);
}

ActivationResult waitForActivationComplete(Gripper& gripper, std::chrono::steady_clock::time_point deadline)
{
   return waitUntil([&] { return gripper.getStatus().gripperStatus.activationState() == ActivationState::Complete; },
                    deadline)
           ? ActivationResult::Activated
           : ActivationResult::Timeout;
}

bool isActivationHandshakeAllowed(std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout)
{
   return deadline - std::chrono::steady_clock::now() >= timeout / 2;
}

// The manual's reset handshake: an rACT falling edge resets the gripper
// (clearing its fault status); the rising edge runs the calibration
// sweep.
ActivationResult runActivationHandshake(Gripper& gripper, std::chrono::steady_clock::time_point deadline)
{
   const GripperCommand previousCommand = gripper.getCommand();

   GripperCommand activateCommand = previousCommand;
   // finishing activation should not start a motion:
   activateCommand.action.set(ActionRequestBit::GoTo, false);
   activateCommand.action.set(ActionRequestBit::Activate, true);
   GripperCommand deactivateCommand = activateCommand;
   deactivateCommand.action.set(ActionRequestBit::Activate, false);

   gripper.setCommand(deactivateCommand);
   if(!waitUntil([&] { return !gripper.getStatus().gripperStatus.activated(); }, deadline))
   {
      gripper.setCommand(previousCommand);
      return ActivationResult::Timeout;
   }

   gripper.setCommand(activateCommand);

   return waitForActivationComplete(gripper, deadline);
}
} // namespace

ActivationResult activate(Gripper& gripper, std::chrono::milliseconds timeout)
{
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   if(!waitOperational(gripper, deadline))
   {
      return ActivationResult::Timeout;
   }

   const GripperStatus status = gripper.getStatus();
   if(severity(status.faultStatus.gripperFault()) == FaultSeverity::Major)
   {
      return ActivationResult::FaultLatched;
   }
   if(status.gripperStatus.activationState() == ActivationState::Complete)
   {
      // Activation survives com loss: the normal host-restart case.
      return ActivationResult::AlreadyActive;
   }
   if(status.gripperStatus.activationState() == ActivationState::InProgress)
   {
      return waitForActivationComplete(gripper, deadline);
   }
   if(!isActivationHandshakeAllowed(deadline, timeout))
   {
      return ActivationResult::Timeout;
   }
   return runActivationHandshake(gripper, deadline);
}

ActivationResult recoverFromFault(Gripper& gripper, std::chrono::milliseconds timeout)
{
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   if(!waitOperational(gripper, deadline) || !isActivationHandshakeAllowed(deadline, timeout))
   {
      return ActivationResult::Timeout;
   }
   return runActivationHandshake(gripper, deadline);
}

} // namespace Robotiq
