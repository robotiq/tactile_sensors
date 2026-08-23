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

} // namespace Robotiq::test
