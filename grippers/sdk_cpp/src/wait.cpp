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

} // namespace Robotiq
