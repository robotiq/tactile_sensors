// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <chrono>

#include <Robotiq/gripper/platform.hpp>

namespace Robotiq::test {
using namespace std::chrono_literals;
using SteadyClock = std::chrono::steady_clock;

TEST(TestStdPlatform, sleep_until_never_wakes_early)
{
   const auto platform = makeDefaultPlatform();
   for(int i = 0; i < 5; ++i)
   {
      const auto deadline = SteadyClock::now() + 3ms;
      platform->sleepUntil(deadline);
      EXPECT_GE(SteadyClock::now(), deadline);
   }
}

TEST(TestStdPlatform, sleep_until_in_the_past_returns_immediately)
{
   const auto platform = makeDefaultPlatform();
   const auto before = SteadyClock::now();
   platform->sleepUntil(before - 1h);
   EXPECT_LT(SteadyClock::now() - before, 1s);
}

TEST(TestStdPlatform, sleep_for_takes_at_least_the_duration)
{
   const auto platform = makeDefaultPlatform();
   const auto before = SteadyClock::now();
   platform->sleepFor(3ms);
   EXPECT_GE(SteadyClock::now() - before, 3ms);
}
} // namespace Robotiq::test
