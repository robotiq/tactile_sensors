// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/gripper/wait.hpp>

#include <chrono>
#include <cstdint>
#include <optional>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/stamped_exchange.hpp>
#include <Robotiq/gripper/status.hpp>

namespace Robotiq {

std::optional<StampedExchange> setCommandAndWaitForExchange(Gripper& gripper,
                                                            const GripperCommand& command,
                                                            std::chrono::milliseconds timeout)
{
   // Counted before the block lands, so a cycle that carries it before the
   // wait starts is still the one returned.
   const uint64_t before = gripper.getMostRecentStampedExchange().metadata.exchangeCount;
   gripper.setCommand(command);
   return detail::waitForExchangeAfter(
      gripper,
      before,
      [&](const StampedExchange& exchange) { return exchange.command == command; },
      timeout);
}

std::optional<StampedExchange> waitForPositionEcho(const Gripper& gripper,
                                                   uint8_t position,
                                                   std::chrono::milliseconds timeout)
{
   return waitFor(
      gripper,
      [position](const StampedExchange& exchange) { return exchange.status.positionRequestEcho == position; },
      timeout);
}

std::optional<StampedExchange> waitForObjectDetection(const Gripper& gripper,
                                                      ObjectDetection detection,
                                                      std::chrono::milliseconds timeout)
{
   return waitFor(
      gripper,
      [detection](const StampedExchange& exchange) {
         return exchange.status.gripperStatus.objectDetection() == detection;
      },
      timeout);
}

std::optional<StampedExchange> waitForMotionEnd(const Gripper& gripper, std::chrono::milliseconds timeout)
{
   return waitFor(
      gripper,
      [](const StampedExchange& exchange) {
         return exchange.status.gripperStatus.objectDetection() != ObjectDetection::Moving;
      },
      timeout);
}

} // namespace Robotiq
