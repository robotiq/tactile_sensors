// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "gripper_state.hpp"

#include <algorithm>
#include <mutex> // std::lock_guard — available even where std::mutex is not
#include <string>
#include <utility>

#include <Robotiq/detail/default_logger.hpp>
#include <Robotiq/gripper/driver_exception.hpp>

namespace Robotiq::detail {
namespace {
//! Consecutive exchange failures before connectionState() degrades to Faulted.
constexpr uint64_t kFaultThreshold = 3;
//! Initial status-read attempts before construction fails.
constexpr uint64_t kInitialReadAttempts = 3;
} // namespace

GripperState::GripperState(std::unique_ptr<Serial> serial,
                           uint8_t slaveAddress,
                           std::chrono::microseconds exchangePeriod,
                           std::shared_ptr<Platform> os,
                           std::shared_ptr<Logger> log)
   : logger(log ? std::move(log) : detail::makeDefaultLogger())
   , platform(std::move(os))
   , client(std::move(serial), slaveAddress, logger)
   , period(exchangePeriod)
   , imageMutex(platform->makeMutex())
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
         fresh = client.readStatus();
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

void GripperState::exchangeOnce()
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

void GripperState::start()
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

void GripperState::stop() noexcept
{
   running.store(false);
   if(exchangeThread)
   {
      exchangeThread->join();
      exchangeThread.reset();
   }
}

} // namespace Robotiq::detail
