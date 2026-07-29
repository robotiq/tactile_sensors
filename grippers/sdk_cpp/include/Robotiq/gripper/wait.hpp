// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Polling helpers for waiting on a condition of the process
//!        image (whose accessors are instant and never block).

#pragma once

#include <chrono>
#include <thread>

namespace Robotiq {

// Poll predicate until it holds (true) or deadline passes (false).
// The predicate is evaluated at least once, even past the deadline: an
// already-true condition never reports a timeout.
template <typename Predicate>
[[nodiscard]] bool waitUntil(Predicate predicate,
                             std::chrono::steady_clock::time_point deadline,
                             std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   while(true)
   {
      if(predicate())
      {
         return true;
      }
      if(std::chrono::steady_clock::now() >= deadline)
      {
         return false;
      }
      std::this_thread::sleep_for(pollPeriod);
   }
}

// Poll predicate until it holds (true) or timeout elapses (false).
template <typename Predicate>
[[nodiscard]] bool waitFor(Predicate predicate,
                           std::chrono::milliseconds timeout,
                           std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return waitUntil(predicate, std::chrono::steady_clock::now() + timeout, pollPeriod);
}
} // namespace Robotiq
