// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>

#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/wait.hpp>

namespace Robotiq::test {
using namespace std::chrono_literals;

namespace {
// Counts the sleeps instead of taking them: proves the polls sleep on the
// injected platform without this test waiting on a real clock.
class CountingPlatform final : public Platform
{
public:
   int sleeps = 0;

   std::unique_ptr<Mutex> makeMutex() override { return nullptr; }
   std::unique_ptr<Thread> spawn(std::function<void()>) override { return nullptr; }
   void sleepUntil(std::chrono::steady_clock::time_point) override {}
   void sleepFor(std::chrono::milliseconds) override { ++sleeps; }
};
} // namespace

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

TEST(TestWait, polls_sleep_on_the_injected_platform)
{
   CountingPlatform platform;
   int calls = 0;
   EXPECT_TRUE(waitUntil([&] { return ++calls >= 3; }, platform, std::chrono::steady_clock::now() + 1h));
   EXPECT_EQ(calls, 3);
   EXPECT_EQ(platform.sleeps, 2); // one sleep between each poll, none after the last
}
} // namespace Robotiq::test
