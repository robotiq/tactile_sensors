// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Default Logger implementation that writes to stderr.
//! Hosted-only: it needs <iostream> and a wall clock, neither of which a
//! freestanding target has — provide an application Logger (e.g. a UART
//! sink) there instead.
//! Thread-safe: a single std::mutex serializes writes so interleaved log
//! lines from concurrent threads aren't garbled.

#pragma once

#include <Robotiq/detail/config.hpp>

#if !GRIPPERS_HOSTED
#error "StderrLogger is hosted-only: on a freestanding target, implement a Logger over your own sink instead."
#endif

#include <mutex>
#include <string_view>

#include <Robotiq/gripper/logger.hpp>

namespace Robotiq {

class StderrLogger : public Logger
{
public:
   void log(Level level, std::string_view message) override;

private:
   std::mutex _mutex;
};

} // namespace Robotiq
