// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <chrono>
#include <utility>

#include <Robotiq/detail/config.hpp>
#include <Robotiq/gripper/platform.hpp>

namespace Robotiq {

//! \brief Polling helpers for waiting on a condition of the process image
//!
//! The overloads taking a Platform sleep on it between polls; the ones
//! without sleep on the default (std::thread-backed) platform and so
//! exist only on hosted runtimes.
//!
//! \par Example
//! \snippet snippets.cpp wait-with-platform

//! \ingroup utilities
//! \brief Poll \p predicate until it holds, or \p deadline passes.
//!
//! \p predicate is evaluated at least once, even past the deadline: an
//! already-true condition never reports a timeout.
//! \tparam Predicate A callable taking no arguments, returning bool.
//! \param predicate The condition to wait for.
//! \param platform Supplies the sleep between polls.
//! \param deadline The time point past which waiting gives up.
//! \param pollPeriod How long to sleep between polls.
//! \return true if \p predicate held before \p deadline; false on timeout.
template <typename Predicate>
bool waitUntil(Predicate predicate,
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

//! \ingroup utilities
//! \brief Poll \p predicate until it holds, or \p timeout elapses.
//! \tparam Predicate A callable taking no arguments, returning bool.
//! \param predicate The condition to wait for.
//! \param platform Supplies the sleep between polls.
//! \param timeout How long to wait, starting now.
//! \param pollPeriod How long to sleep between polls.
//! \return true if \p predicate held within \p timeout; false on timeout.
template <typename Predicate>
bool waitFor(Predicate predicate,
             Platform& platform,
             std::chrono::milliseconds timeout,
             std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return waitUntil(std::move(predicate), platform, std::chrono::steady_clock::now() + timeout, pollPeriod);
}

#if GRIPPERS_HOSTED
//! \ingroup utilities
//! \overload
//! Sleeps on the default (std::thread-backed) platform. Hosted-only.
template <typename Predicate>
bool waitUntil(Predicate predicate,
               std::chrono::steady_clock::time_point deadline,
               std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return waitUntil(std::move(predicate), *makeDefaultPlatform(), deadline, pollPeriod);
}

//! \ingroup utilities
//! \overload
//! Sleeps on the default (std::thread-backed) platform. Hosted-only.
template <typename Predicate>
bool waitFor(Predicate predicate,
             std::chrono::milliseconds timeout,
             std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return waitUntil(std::move(predicate), std::chrono::steady_clock::now() + timeout, pollPeriod);
}
#endif // GRIPPERS_HOSTED

} // namespace Robotiq
