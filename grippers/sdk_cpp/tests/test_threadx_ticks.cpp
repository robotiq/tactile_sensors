// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Host tests for the parts of the ThreadX port that are pure
//! arithmetic, and so testable without the kernel.
//!
//! \note ULONG is 64-bit on the host and 32-bit on the target, so the
//! truncation the target would suffer is not reproduced. The assertions are
//! written against TX_WAIT_FOREVER and monotonicity instead, which hold at
//! either width.

#include <gtest/gtest.h>

#include <chrono>
#include <limits>

#include <threadx_ticks.hpp>

namespace Robotiq::ports::detail {
namespace {
using namespace std::chrono_literals;

//! The deadline that maps to exactly 0xFFFFFFFF ticks at 100 Hz.
constexpr std::chrono::nanoseconds kSentinelDeadline{42949672950000000LL};
constexpr std::chrono::nanoseconds kUnbounded{std::numeric_limits<long long>::max()};

TEST(ThreadXBlockingTicks, ordinary_durations_round_up_to_whole_ticks)
{
   EXPECT_EQ(blockingTicks(10ms), 1UL); // Exactly one tick at 100 Hz.
   EXPECT_EQ(blockingTicks(1s), 100UL);
   EXPECT_EQ(blockingTicks(1ns), 1UL); // Rounded up, never rounded away.
}

TEST(ThreadXBlockingTicks, nothing_to_wait_for_does_not_block)
{
   // 0 is TX_NO_WAIT to a semaphore get, and no sleep at all otherwise.
   EXPECT_EQ(blockingTicks(std::chrono::nanoseconds::zero()), 0UL);
   EXPECT_EQ(blockingTicks(-1s), 0UL);
}

TEST(ThreadXBlockingTicks, no_finite_deadline_becomes_an_infinite_wait)
{
   // 0xFFFFFFFF is TX_WAIT_FOREVER, so a deadline landing on it stops being
   // a deadline at all. Before the clamp this returned it exactly.
   EXPECT_NE(blockingTicks(kSentinelDeadline), TX_WAIT_FOREVER);
   EXPECT_NE(blockingTicks(kSentinelDeadline + 1s), TX_WAIT_FOREVER);
   EXPECT_NE(blockingTicks(kUnbounded), TX_WAIT_FOREVER);
}

TEST(ThreadXBlockingTicks, the_result_always_fits_the_targets_thirty_two_bits)
{
   // Asserted rather than reproduced: ULONG is 64-bit here, so a bound that
   // holds at either width is worth more than a build that models one.
   EXPECT_LE(blockingTicks(kUnbounded), TX_WAIT_FOREVER - 1);
   EXPECT_LE(blockingTicks(kSentinelDeadline + 1s), TX_WAIT_FOREVER - 1);
}

TEST(ThreadXBlockingTicks, a_longer_wait_never_yields_fewer_ticks)
{
   // The property a wrap breaks: the old form overflowed a long long past
   // about three years, so time_point::max() came back below one second's
   // worth of ticks and the wait collapsed to nothing.
   const std::chrono::nanoseconds ascending[] = {
      1ns,
      10ms,
      1s,
      1h,
      24h * 365,
      kSentinelDeadline,
      kUnbounded,
   };
   ULONG previous = 0;
   for(const auto duration : ascending)
   {
      const ULONG ticks = blockingTicks(duration);
      EXPECT_GE(ticks, previous) << "at " << duration.count() << " ns";
      previous = ticks;
   }
}

TEST(ThreadXBlockingTicks, a_deadline_already_reached_does_not_block)
{
   // What the call sites hand in: the remainder of a deadline, which goes
   // non-positive the moment the deadline is reached.
   const auto now = std::chrono::steady_clock::now();
   EXPECT_EQ(blockingTicks((now - 1s) - now), 0UL);
   EXPECT_EQ(blockingTicks(now - now), 0UL); // The boundary, exactly reached.
   EXPECT_EQ(blockingTicks((now + 1ns) - now), 1UL);
}

TEST(ThreadXBlockingTicks, an_unbounded_deadline_stays_a_finite_wait)
{
   const auto now = std::chrono::steady_clock::now();
   const ULONG ticks = blockingTicks(std::chrono::steady_clock::time_point::max() - now);
   EXPECT_NE(ticks, TX_WAIT_FOREVER);
   EXPECT_LE(ticks, TX_WAIT_FOREVER - 1);
   EXPECT_GT(ticks, 0UL);
}

} // namespace
} // namespace Robotiq::ports::detail
