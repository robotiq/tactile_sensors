// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Polling helpers for waiting on a condition of the process
//!        image (whose accessors are instant and never block).
//! The overloads taking a Platform sleep on it between polls;
//! the ones without sleep on the default (std::thread-backed) platform
//! and so exist only on hosted runtimes.

#pragma once

#include <chrono>
#include <utility>

#include <Robotiq/detail/config.hpp>
#include <Robotiq/gripper/platform.hpp>

namespace Robotiq {

// Poll predicate until it holds (true) or deadline passes (false).
// The predicate is evaluated at least once, even past the deadline: an
// already-true condition never reports a timeout.
template <typename Predicate>
[[nodiscard]] bool waitUntil(Predicate predicate,
                             Platform& platform,
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
      platform.sleepFor(pollPeriod);
   }
}

// Poll predicate until it holds (true) or timeout elapses (false).
template <typename Predicate>
[[nodiscard]] bool waitFor(Predicate predicate,
                           Platform& platform,
                           std::chrono::milliseconds timeout,
                           std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return waitUntil(std::move(predicate), platform, std::chrono::steady_clock::now() + timeout, pollPeriod);
}

#if GRIPPERS_HOSTED
template <typename Predicate>
[[nodiscard]] bool waitUntil(Predicate predicate,
                             std::chrono::steady_clock::time_point deadline,
                             std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return waitUntil(std::move(predicate), *makeDefaultPlatform(), deadline, pollPeriod);
}

template <typename Predicate>
[[nodiscard]] bool waitFor(Predicate predicate,
                           std::chrono::milliseconds timeout,
                           std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return waitUntil(std::move(predicate), std::chrono::steady_clock::now() + timeout, pollPeriod);
}
#endif // GRIPPERS_HOSTED

} // namespace Robotiq
