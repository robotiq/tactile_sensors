// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Rate limiter for a single call site. Internal: it exists to keep
//!        the SDK's own periodic log lines from flooding a sink. Owned by
//!        the call site, so independent call sites throttle independently.
//!        Not thread-safe: one instance, one thread.

#pragma once

#include <chrono>
#include <utility>

namespace Robotiq::detail {
class Throttle
{
public:
   explicit Throttle(std::chrono::milliseconds period)
      : _period(period)
   {
   }

   // Run \p callable at most once per period; suppressed calls are dropped.
   template <typename Callable>
   void executeIfAllowed(Callable&& callable)
   {
      executeIfAllowed(std::chrono::steady_clock::now(), std::forward<Callable>(callable));
   }

   // Deterministic variant for callers that already hold a timestamp.
   template <typename Callable>
   void executeIfAllowed(std::chrono::steady_clock::time_point now, Callable&& callable)
   {
      if(now - _last < _period)
      {
         return;
      }
      _last = now;
      callable();
   }

private:
   std::chrono::milliseconds _period;
   std::chrono::steady_clock::time_point _last{};
};
} // namespace Robotiq::detail
