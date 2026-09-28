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
