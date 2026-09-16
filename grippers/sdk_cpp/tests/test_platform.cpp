// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/wait.hpp>

namespace Robotiq::test {
namespace {
constexpr int kWaiters = 4;
//! Long enough that a scheduling hiccup cannot pass for an early return,
//! short enough to pay four times over in a unit test.
constexpr std::chrono::milliseconds kDeadline{200};
} // namespace

class TestConditionVariable : public ::testing::Test
{
protected:
   std::shared_ptr<Platform> platform = makeDefaultPlatform();
   std::unique_ptr<Mutex> mutex = platform->makeMutex();
   std::unique_ptr<ConditionVariable> condition = platform->makeConditionVariable();
};

TEST_F(TestConditionVariable, the_hosted_platform_offers_one)
{
   EXPECT_NE(condition, nullptr);
}

TEST_F(TestConditionVariable, one_notification_wakes_every_waiter)
{
   ASSERT_NE(condition, nullptr);
   std::atomic<int> waiting{0};
   std::atomic<int> woken{0};
   bool signalled = false; // guarded by mutex

   std::vector<std::thread> waiters;
   for(int waiter = 0; waiter < kWaiters; ++waiter)
   {
      waiters.emplace_back([&] {
         const std::lock_guard<Mutex> lock(*mutex);
         ++waiting;
         while(!signalled)
         {
            condition->waitUntil(*mutex, std::chrono::steady_clock::now() + std::chrono::seconds(5));
         }
         ++woken;
      });
   }

   ASSERT_TRUE(waitFor([&] { return waiting.load() == kWaiters; }, std::chrono::seconds(2)));
   // \__All threads are waiting for the condition to notify

   {
      const std::lock_guard<Mutex> lock(*mutex);
      signalled = true;
   }
   condition->notifyAll();

   // notifyAll(), not one-at-a-time: every waiter is woken.
   EXPECT_TRUE(waitFor([&] { return woken.load() == kWaiters; }, std::chrono::seconds(2)));
   for(auto& waiter : waiters)
   {
      waiter.join();
   }
   EXPECT_EQ(woken.load(), kWaiters);
}

//! The tests below are the contract every Platform implementation has to
//! meet, not coverage of the hosted one: an RTOS port that emulates a
//! condition variable over its native primitives should be run against them.
//! They cover the paths that do not end in a notification, which is where an
//! emulation tends to diverge.

TEST_F(TestConditionVariable, waiting_returns_when_the_deadline_expires)
{
   ASSERT_NE(condition, nullptr);
   const auto deadline = std::chrono::steady_clock::now() + kDeadline;

   const std::lock_guard<Mutex> lock(*mutex);
   condition->waitUntil(*mutex, deadline);

   // Both halves matter: reaching this line at all rules out a wait that
   // blocks forever with nothing to notify it, and the deadline having
   // passed rules out a busy-spin that returns at once and calls it a wait.
   EXPECT_GE(std::chrono::steady_clock::now(), deadline);
}

TEST_F(TestConditionVariable, notifying_with_no_waiters_is_not_remembered)
{
   ASSERT_NE(condition, nullptr);
   condition->notifyAll(); // Nobody is waiting.

   const auto deadline = std::chrono::steady_clock::now() + kDeadline;
   const std::lock_guard<Mutex> lock(*mutex);
   condition->waitUntil(*mutex, deadline);

   // A notification nobody heard is not banked for the next waiter. An
   // emulation that leaves a token behind returns here immediately, turning
   // the next unrelated wait into a spurious wake.
   EXPECT_GE(std::chrono::steady_clock::now(), deadline);
}

TEST_F(TestConditionVariable, a_deadline_already_past_returns_at_once)
{
   ASSERT_NE(condition, nullptr);
   const auto before = std::chrono::steady_clock::now();

   // Held across the call on purpose: this also asserts waitUntil relocks on
   // the way out, which no other path here checks. The guard would deadlock
   // or abort on release if it did not.
   const std::lock_guard<Mutex> lock(*mutex);
   condition->waitUntil(*mutex, before - std::chrono::seconds(1));

   // The degenerate case every predicate loop hits once the caller's own
   // timeout has run out, so callers do reach it.
   EXPECT_LT(std::chrono::steady_clock::now() - before, kDeadline);
}

} // namespace Robotiq::test
