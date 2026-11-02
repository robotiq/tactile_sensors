// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/gripper.hpp>

#include "gripper_state.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/driver_exception.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/serial.hpp>
#include <Robotiq/gripper/wait.hpp>

namespace Robotiq {
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
   : _impl(std::make_unique<detail::GripperState>(std::move(serial),
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
   _impl->setCommand(command);
}

GripperCommand Gripper::getCommand() const
{
   return _impl->command();
}

GripperStatus Gripper::getStatus() const
{
   return _impl->status();
}

StampedExchange Gripper::getMostRecentStampedExchange() const
{
   return _impl->stampedExchange();
}

std::optional<StampedExchange> Gripper::waitForExchange(std::chrono::milliseconds timeout) const
{
   return waitForExchangeCount(_impl->stampedExchange().metadata.exchangeCount + 1, timeout);
}

std::optional<StampedExchange> Gripper::waitForExchangeCount(uint64_t desiredExchangeCount,
                                                             std::chrono::milliseconds timeout) const
{
   const StampedExchange fresh = _impl->waitForExchange(desiredExchangeCount, detail::deadlineAfter(timeout));
   if(fresh.metadata.exchangeCount < desiredExchangeCount)
   {
      // timeout or dead gripper
      return std::nullopt;
   }
   return fresh;
}

ConnectionState Gripper::connectionState() const
{
   return _impl->connectionState();
}

Platform& Gripper::platform() const noexcept
{
   return _impl->platform();
}

namespace {
// Each step of the blocking procedures completes on the exchange that
// shows it, and judges the status of that exchange rather than a status
// read afterwards.

std::chrono::milliseconds remaining(std::chrono::steady_clock::time_point deadline)
{
   const auto now = std::chrono::steady_clock::now();
   return deadline > now ? std::chrono::ceil<std::chrono::milliseconds>(deadline - now) : std::chrono::milliseconds(0);
}

template <typename Predicate>
std::optional<StampedExchange> waitForStatus(const Gripper& gripper,
                                             Predicate predicate,
                                             std::chrono::steady_clock::time_point deadline)
{
   return waitFor(
      gripper,
      [&](const StampedExchange& exchange) { return predicate(exchange.status); },
      remaining(deadline));
}

// The exchange cycle must be delivering fresh status before a procedure
// can judge the gripper: a completed exchange is that proof, while a link
// that stays Faulted completes none, which no command can fix. Gripper
// faults (gFLT) are the callers' business.
std::optional<StampedExchange> waitOperational(const Gripper& gripper, std::chrono::steady_clock::time_point deadline)
{
   return waitForStatus(gripper, [](const GripperStatus&) { return true; }, deadline);
}

ActivationResult waitForActivationComplete(const Gripper& gripper, std::chrono::steady_clock::time_point deadline)
{
   return waitForStatus(
             gripper,
             [](const GripperStatus& status) {
                return status.gripperStatus.activationState() == ActivationState::Complete;
             },
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
   if(!waitForStatus(gripper, [](const GripperStatus& status) { return !status.gripperStatus.activated(); }, deadline))
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
   const std::optional<StampedExchange> fresh = waitOperational(gripper, deadline);
   if(!fresh)
   {
      return ActivationResult::Timeout;
   }

   const GripperStatus& status = fresh->status;
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
