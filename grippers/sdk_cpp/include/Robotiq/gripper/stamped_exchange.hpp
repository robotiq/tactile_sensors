// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <chrono>
#include <cstdint>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/status.hpp>

namespace Robotiq {

//! \ingroup status
//! \brief Exchange metadata.
struct ExchangeMetadata
{
   uint64_t exchangeCount = 0; //!< Completed exchanges, this one included.
   std::chrono::steady_clock::time_point timestamp{}; //!< When the exchange completed.
};

//! \ingroup status
//! \brief Stamped exchange.
struct StampedExchange
{
   ExchangeMetadata metadata; //!< What names the exchange: count and instant.
   GripperCommand command; //!< The command block the exchange wrote.
   GripperStatus status; //!< The status block it read back.
};

} // namespace Robotiq
