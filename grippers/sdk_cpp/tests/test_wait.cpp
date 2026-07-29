// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <chrono>

#include <Robotiq/gripper/wait.hpp>

namespace Robotiq::test {
using namespace std::chrono_literals;

TEST(TestWait, already_true_predicate_succeeds_with_a_zero_timeout)
{
   int calls = 0;
   EXPECT_TRUE(waitFor(
      [&] {
         ++calls;
         return true;
      },
      0ms));
   EXPECT_EQ(calls, 1);
}

TEST(TestWait, already_true_predicate_succeeds_past_the_deadline)
{
   EXPECT_TRUE(waitUntil([] { return true; }, std::chrono::steady_clock::now() - 1s));
}

TEST(TestWait, false_predicate_times_out)
{
   EXPECT_FALSE(waitFor([] { return false; }, 0ms));
   EXPECT_FALSE(waitFor([] { return false; }, 5ms, 1ms));
}

TEST(TestWait, condition_turning_true_is_seen)
{
   int calls = 0;
   EXPECT_TRUE(waitFor([&] { return ++calls >= 3; }, 2s, 1ms));
   EXPECT_EQ(calls, 3);
}
} // namespace Robotiq::test
