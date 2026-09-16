// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The port's tick arithmetic, split out from threadx_platform.hpp so
//! it can be tested on a host.

#pragma once

#include <algorithm>
#include <chrono>

#include "tx_api.h"

namespace Robotiq::ports::detail {

//! Ticks to block for, or 0 for "do not block at all" — TX_NO_WAIT to a
//! semaphore get, and nothing worth sleeping for to tx_thread_sleep().
//!
//! Rounds up, so any time left at all is worth at least one tick, and
//! saturates below TX_WAIT_FOREVER so a finite deadline never becomes an
//! infinite wait.
inline ULONG blockingTicks(std::chrono::nanoseconds duration)
{
   // nanoseconds::rep is used because it is fixed width int64_t on every toolchain we
   // build for, and the standard already guarantees it at least 64 bits.
   using Count = std::chrono::nanoseconds::rep;
   constexpr Count kNsPerSecond = 1000000000;
   constexpr Count kPerSecond = TX_TIMER_TICKS_PER_SECOND;
   constexpr Count kMaxTicks = static_cast<Count>(TX_WAIT_FOREVER) - 1;
   constexpr Count kMaxNs = kMaxTicks * kNsPerSecond / kPerSecond;
   if(duration <= std::chrono::nanoseconds::zero())
   {
      return 0UL;
   }
   const Count ns = std::min(duration.count(), kMaxNs);
   return static_cast<ULONG>((ns * kPerSecond + kNsPerSecond - 1) / kNsPerSecond);
}

} // namespace Robotiq::ports::detail
