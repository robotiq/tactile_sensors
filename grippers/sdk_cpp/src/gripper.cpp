// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/gripper.hpp>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex> // std::lock_guard — available even where std::mutex is not
#include <utility>

#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/driver_exception.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/throttle.hpp>
#include <Robotiq/detail/gripper_modbus_client.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/serial.hpp>

namespace Robotiq {
namespace {
//! Consecutive exchange failures before state() degrades to Faulted.
constexpr uint64_t kFaultThreshold = 3;
//! Initial status-read attempts before construction fails.
constexpr uint64_t kInitialReadAttempts = 3;
} // namespace

struct Gripper::Impl
{
   std::shared_ptr<Logger> logger;
   std::shared_ptr<Platform> platform;
   detail::GripperModbusClient client;
   Throttle failureLogThrottle{std::chrono::milliseconds(1000)};
   std::chrono::microseconds period;

   const std::unique_ptr<Mutex> imageMutex;
   GripperCommand command{};
   GripperStatus status{};

   std::atomic<ConnectionState> state{ConnectionState::Connecting};
   std::atomic<bool> running{false};
   // 32-bit: a 64-bit atomic needs __atomic_*_8 (no native 8-byte atomic on a
   // 32-bit MCU); a failure counter never needs more than 32 bits.
   std::atomic<uint32_t> consecutiveFailures{0};
   std::unique_ptr<Thread> exchangeThread;

   Impl(std::unique_ptr<Serial> serial,
        uint8_t slaveAddress,
        std::chrono::microseconds exchangePeriod,
        std::shared_ptr<Platform> os,
        std::shared_ptr<Logger> log)
      : logger(log ? std::move(log) : makeDefaultLogger())
      , platform(std::move(os))
      , client(std::move(serial), slaveAddress, logger)
      , period(exchangePeriod)
      , imageMutex(platform->makeMutex())
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
            logger->log(Logger::Level::Warn,
                        "initial status read attempt " + std::to_string(attempt) + " of "
                           + std::to_string(kInitialReadAttempts) + " failed: " + ex.what());
         }
      }

      const std::lock_guard<Mutex> lock(*imageMutex);
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
         const std::lock_guard<Mutex> lock(*imageMutex);
         commandCopy = command;
      }

      GripperStatus freshStatus;
      try
      {
         freshStatus = client.exchange(commandCopy);
      }
      catch(...)
      {
         if(consecutiveFailures.fetch_add(1) + 1 >= kFaultThreshold
            && state.exchange(ConnectionState::Faulted) != ConnectionState::Faulted)
         {
            logger->log(Logger::Level::Warn,
                        "link faulted after " + std::to_string(kFaultThreshold)
                           + " consecutive failed exchanges; the process image is now stale");
         }
         throw;
      }

      {
         const std::lock_guard<Mutex> lock(*imageMutex);
         status = freshStatus;
      }
      consecutiveFailures.store(0);
      if(state.exchange(ConnectionState::Operational) == ConnectionState::Faulted)
      {
         logger->log(Logger::Level::Info, "link recovered; the process image is live again");
      }
   }

   void start()
   {
      running.store(true);
      exchangeThread = platform->spawn([this] {
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
            platform->sleepUntil(nextCycle);
         }
      });
   }

   void stop() noexcept
   {
      running.store(false);
      if(exchangeThread)
      {
         exchangeThread->join();
         exchangeThread.reset();
      }
   }
};

namespace {
std::shared_ptr<Platform> checkedPlatform(std::shared_ptr<Platform> platform)
{
   if(!platform)
   {
      throw DriverException("a null Platform was passed — pass your RTOS platform, or use a hosted constructor");
   }
   return platform;
}
} // namespace

Gripper::Gripper(std::unique_ptr<Serial> serial,
                 uint8_t slaveAddress,
                 std::chrono::microseconds exchangePeriod,
                 std::shared_ptr<Platform> platform,
                 std::shared_ptr<Logger> logger)
   : _impl(std::make_unique<Impl>(std::move(serial),
                                  slaveAddress,
                                  exchangePeriod,
                                  checkedPlatform(std::move(platform)),
                                  std::move(logger)))
{
   _impl->initializeImage();
   _impl->start();
}

Gripper::~Gripper() = default;

void Gripper::setCommand(const GripperCommand& command)
{
   const std::lock_guard<Mutex> lock(*_impl->imageMutex);
   _impl->command = command;
}

GripperCommand Gripper::getCommand() const
{
   const std::lock_guard<Mutex> lock(*_impl->imageMutex);
   return _impl->command;
}

GripperStatus Gripper::getStatus() const
{
   const std::lock_guard<Mutex> lock(*_impl->imageMutex);
   return _impl->status;
}

ConnectionState Gripper::connectionState() const
{
   return _impl->state.load();
}

Platform& Gripper::platform() const noexcept
{
   return *_impl->platform;
}

namespace {
// The blocking procedures sleep on the gripper's own Platform between polls
// (hosted or RTOS alike, wherever that gripper runs), so the helpers all
// take it alongside the gripper.

// The exchange cycle must be delivering fresh status before a procedure
// can judge the gripper: Faulted here is link health, which no command
// can fix. Gripper faults (gFLT) are the callers' business.
bool waitOperational(const Gripper& gripper, Platform& platform, std::chrono::steady_clock::time_point deadline)
{
   return waitUntil([&] { return gripper.connectionState() == ConnectionState::Operational; }, platform, deadline);
}

ActivationResult waitForActivationComplete(Gripper& gripper,
                                           Platform& platform,
                                           std::chrono::steady_clock::time_point deadline)
{
   return waitUntil([&] { return gripper.getStatus().gripperStatus.activationState() == ActivationState::Complete; },
                    platform,
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
ActivationResult runActivationHandshake(Gripper& gripper,
                                        Platform& platform,
                                        std::chrono::steady_clock::time_point deadline)
{
   const GripperCommand previousCommand = gripper.getCommand();

   GripperCommand activateCommand = previousCommand;
   // finishing activation should not start a motion:
   activateCommand.action.set(ActionRequestBit::GoTo, false);
   activateCommand.action.set(ActionRequestBit::Activate, true);
   GripperCommand deactivateCommand = activateCommand;
   deactivateCommand.action.set(ActionRequestBit::Activate, false);

   gripper.setCommand(deactivateCommand);
   if(!waitUntil([&] { return !gripper.getStatus().gripperStatus.activated(); }, platform, deadline))
   {
      gripper.setCommand(previousCommand);
      return ActivationResult::Timeout;
   }

   gripper.setCommand(activateCommand);

   return waitForActivationComplete(gripper, platform, deadline);
}
} // namespace

ActivationResult activate(Gripper& gripper, std::chrono::milliseconds timeout)
{
   Platform& platform = gripper.platform();
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   if(!waitOperational(gripper, platform, deadline))
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
      return waitForActivationComplete(gripper, platform, deadline);
   }
   if(!isActivationHandshakeAllowed(deadline, timeout))
   {
      return ActivationResult::Timeout;
   }
   return runActivationHandshake(gripper, platform, deadline);
}

ActivationResult recoverFromFault(Gripper& gripper, std::chrono::milliseconds timeout)
{
   Platform& platform = gripper.platform();
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   if(!waitOperational(gripper, platform, deadline) || !isActivationHandshakeAllowed(deadline, timeout))
   {
      return ActivationResult::Timeout;
   }
   return runActivationHandshake(gripper, platform, deadline);
}

} // namespace Robotiq
