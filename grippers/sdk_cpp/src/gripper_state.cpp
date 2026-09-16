// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "gripper_state.hpp"

#include <algorithm>
#include <memory>
#include <mutex> // std::lock_guard — available even where std::mutex is not
#include <string>
#include <utility>

#include <Robotiq/detail/default_logger.hpp>
#include <Robotiq/gripper/driver_exception.hpp>

namespace Robotiq::detail {
namespace {
//! Consecutive exchange failures before connectionState() degrades to Faulted.
constexpr uint64_t kFaultThreshold = 3;
//! Initial _status-read attempts before construction fails.
constexpr uint64_t kInitialReadAttempts = 3;

std::unique_ptr<ConditionVariable> checkedConditionVariable(Platform& platform)
{
   auto condition = platform.makeConditionVariable();
   if(!condition)
   {
      throw DriverException("a Platform returned a null ConditionVariable — GripperSync has nothing to "
                            "wait on");
   }
   return condition;
}
} // namespace

GripperState::GripperState(std::unique_ptr<Serial> serial,
                           uint8_t slaveAddress,
                           std::chrono::microseconds exchangePeriod,
                           std::shared_ptr<Platform> os,
                           std::shared_ptr<Logger> log)
   : _logger(log ? std::move(log) : detail::makeDefaultLogger())
   , _platform(std::move(os))
   , _client(std::move(serial), slaveAddress, _logger)
   , _period(exchangePeriod)
   , _imageMutex(_platform->makeMutex())
   , _statusRefreshed(checkedConditionVariable(*_platform))
{
}

GripperState::~GripperState()
{
   stop();
}

void GripperState::initializeImage()
{
   GripperStatus fresh;
   for(uint64_t attempt = 1;; ++attempt)
   {
      try
      {
         fresh = _client.readStatus();
         break;
      }
      catch(const std::exception& ex)
      {
         if(attempt >= kInitialReadAttempts)
         {
            throw DriverException("no gripper answered the initial status read (is it powered, and "
                                  "do its baud rate and Modbus slave address match this "
                                  "connection?) — last attempt: "
                                  + std::string(ex.what()));
         }
         _logger->log(Logger::Level::Warn,
                      "initial status read attempt " + std::to_string(attempt) + " of "
                         + std::to_string(kInitialReadAttempts) + " failed: " + ex.what());
      }
   }

   const std::lock_guard<Mutex> lock(*_imageMutex);
   _status = fresh;
   _statusTimestamp = std::chrono::steady_clock::now();
   _command = GripperCommand::defaults();
   _command.action.set(ActionRequestBit::Activate, fresh.gripperStatus.activated());
   _command.action.set(ActionRequestBit::GoTo, fresh.gripperStatus.goToEnabled());
   _command.positionRequest = fresh.positionRequestEcho;
   _connectionState.store(ConnectionState::Operational);
}

void GripperState::exchangeOnce()
{
   GripperCommand commandCopy;
   {
      const std::lock_guard<Mutex> lock(*_imageMutex);
      commandCopy = _command;
   }

   GripperStatus freshStatus;
   std::chrono::steady_clock::time_point completedAt;
   try
   {
      freshStatus = _client.exchange(commandCopy);
      // Taken before the lock: the closest this side of the wire gets to when
      // the gripper sampled, and never delayed by a reader holding the image.
      completedAt = std::chrono::steady_clock::now();
   }
   catch(...)
   {
      if(_consecutiveFailures.fetch_add(1) + 1 >= kFaultThreshold
         && _connectionState.exchange(ConnectionState::Faulted) != ConnectionState::Faulted)
      {
         _logger->log(Logger::Level::Warn,
                      "link faulted after " + std::to_string(kFaultThreshold)
                         + " consecutive failed exchanges; the process image is now stale");
      }
      throw;
   }

   {
      const std::lock_guard<Mutex> lock(*_imageMutex);
      _status = freshStatus;
      _statusTimestamp = completedAt;
      ++_exchangeCount;
   }
   // Notified with the lock dropped: waiters registered under it, so
   // none can miss this, and the cycle never queues behind one.
   _statusRefreshed->notifyAll();
   _consecutiveFailures.store(0);
   if(_connectionState.exchange(ConnectionState::Operational) == ConnectionState::Faulted)
   {
      _logger->log(Logger::Level::Info, "link recovered; the process image is live again");
   }
}

void GripperState::start()
{
   _running.store(true);
   _exchangeThread = _platform->spawn([this] {
      auto nextCycle = std::chrono::steady_clock::now();
      while(_running.load())
      {
         try
         {
            exchangeOnce();
         }
         catch(const std::exception& ex)
         {
            _failureLogThrottle.executeIfAllowed(
               [&] { _logger->log(Logger::Level::Warn, std::string("exchange cycle failed: ") + ex.what()); });
         }
         catch(...)
         {
            _failureLogThrottle.executeIfAllowed(
               [&] { _logger->log(Logger::Level::Warn, "exchange cycle failed: unknown exception"); });
         }
         // Overrun cycles (e.g. timeouts during a fault) must not
         // accumulate a backlog that bursts exchanges on recovery.
         nextCycle = std::max(nextCycle + _period, std::chrono::steady_clock::now());
         _platform->sleepUntil(nextCycle);
      }
   });
}

void GripperState::setCommand(const GripperCommand& command)
{
   const std::lock_guard<Mutex> lock(*_imageMutex);
   _command = command;
}

GripperCommand GripperState::command() const
{
   const std::lock_guard<Mutex> lock(*_imageMutex);
   return _command;
}

GripperStatus GripperState::status() const
{
   const std::lock_guard<Mutex> lock(*_imageMutex);
   return _status;
}

StampedStatus GripperState::stampedStatus() const
{
   const std::lock_guard<Mutex> lock(*_imageMutex);
   return {_exchangeCount, _status, _statusTimestamp};
}

void GripperState::stop() noexcept
{
   _running.store(false);
   _statusRefreshed->notifyAll();
   if(_exchangeThread)
   {
      _exchangeThread->join();
      _exchangeThread.reset();
   }
}

} // namespace Robotiq::detail
