// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/status.hpp>

#include "instrumented_platform.hpp"
#include "process_image.hpp"

namespace Robotiq::detail {
namespace {
using Robotiq::test::InstrumentedPlatform;
using namespace std::chrono_literals;

//! The waiter increments the count before it enters the real wait, and holds
//! the image lock until that wait releases it — so once this returns, anything
//! that takes the lock (publish, close) runs strictly after the waiter is
//! blocked. No sleep, no timing.
void untilWaiting(const InstrumentedPlatform& platform)
{
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
   while(platform.conditionWaits.load() == 0)
   {
      ASSERT_LT(std::chrono::steady_clock::now(), deadline) << "the waiter never entered the condition variable";
      std::this_thread::yield();
   }
}

GripperStatus statusAt(uint8_t position)
{
   GripperStatus status;
   status.position = position;
   return status;
}

class TestProcessImage : public ::testing::Test
{
protected:
   std::shared_ptr<Platform> platform = makeDefaultPlatform();
   ProcessImage image{*platform};
   const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
};

TEST_F(TestProcessImage, starts_empty_with_no_exchange_counted)
{
   const StampedStatus stamped = image.stampedStatus();
   EXPECT_EQ(stamped.exchangeCount, 0u);
   EXPECT_EQ(stamped.status, GripperStatus{});
   EXPECT_EQ(stamped.timestamp, std::chrono::steady_clock::time_point{});
}

TEST_F(TestProcessImage, seeding_stores_both_blocks_without_counting_a_cycle)
{
   GripperCommand command = GripperCommand::defaults();
   command.positionRequest = 42;
   image.seed(statusAt(7), t0, command);

   EXPECT_EQ(image.status().position, 7);
   EXPECT_EQ(image.command().positionRequest, 42);
   const StampedStatus stamped = image.stampedStatus();
   EXPECT_EQ(stamped.exchangeCount, 0u);
   EXPECT_EQ(stamped.timestamp, t0);
}

TEST_F(TestProcessImage, a_set_command_reads_back_and_leaves_the_status_alone)
{
   image.seed(statusAt(7), t0, GripperCommand::defaults());
   GripperCommand command = GripperCommand::defaults();
   command.positionRequest = 200;
   image.setCommand(command);

   EXPECT_EQ(image.command().positionRequest, 200);
   EXPECT_EQ(image.status().position, 7);
}

TEST_F(TestProcessImage, each_publish_counts_stamps_and_replaces_the_status_together)
{
   image.publish(statusAt(10), t0 + 10ms);
   image.publish(statusAt(20), t0 + 20ms);

   const StampedStatus stamped = image.stampedStatus();
   EXPECT_EQ(stamped.exchangeCount, 2u);
   EXPECT_EQ(stamped.status.position, 20);
   EXPECT_EQ(stamped.timestamp, t0 + 20ms);
   // The plain status view agrees with the stamped one.
   EXPECT_EQ(image.status().position, 20);
}

TEST_F(TestProcessImage, publishing_does_not_touch_the_command)
{
   GripperCommand command = GripperCommand::defaults();
   command.positionRequest = 99;
   image.setCommand(command);
   image.publish(statusAt(1), t0);

   EXPECT_EQ(image.command().positionRequest, 99);
}

TEST(TestProcessImageWaits, a_sync_on_a_count_already_passed_never_enters_a_wait)
{
   InstrumentedPlatform platform;
   ProcessImage image{platform};
   image.publish(statusAt(3), std::chrono::steady_clock::now());

   const StampedStatus reached = image.sync(0, std::chrono::steady_clock::now() + std::chrono::seconds(5));

   EXPECT_EQ(reached.exchangeCount, 1u);
   EXPECT_EQ(platform.conditionWaits.load(), 0);
}

TEST(TestProcessImageWaits, a_publish_from_another_thread_wakes_a_sync)
{
   InstrumentedPlatform platform;
   ProcessImage image{platform};
   std::thread publisher([&] {
      untilWaiting(platform);
      image.publish(statusAt(5), std::chrono::steady_clock::now());
   });
   const StampedStatus reached = image.sync(0, std::chrono::steady_clock::now() + std::chrono::seconds(5));
   publisher.join();
   EXPECT_EQ(reached.exchangeCount, 1u);
   EXPECT_EQ(reached.status.position, 5);
   EXPECT_GE(platform.conditionWaits.load(), 1); // A real wait, ended by the publish.
}

TEST(TestProcessImageWaits, closing_returns_every_waiter_and_every_later_wait_at_once)
{
   InstrumentedPlatform platform;
   ProcessImage image{platform};
   std::thread closer([&] {
      untilWaiting(platform);
      image.close();
   });
   const StampedStatus reached = image.sync(0, std::chrono::steady_clock::now() + std::chrono::seconds(5));
   closer.join();
   EXPECT_EQ(reached.exchangeCount, 0u); // Nothing was published; the close is what returned.
   EXPECT_GE(platform.conditionWaits.load(), 1);

   // Once closed, a later wait does not even enter the condition variable.
   const int waitsSoFar = platform.conditionWaits.load();
   (void)image.sync(0, std::chrono::steady_clock::now() + std::chrono::seconds(5));
   EXPECT_EQ(platform.conditionWaits.load(), waitsSoFar);
}

TEST(TestProcessImageWaits, a_wake_with_nothing_behind_it_is_re_checked_not_trusted)
{
   // A notification carrying no new status is what a spurious wake is, and
   // what the ThreadX emulation can produce from a leftover token. A sync
   // that returned on it without looking at the count would hand back a
   // status it never received.
   InstrumentedPlatform platform;
   ProcessImage image{platform};
   image.publish(statusAt(1), std::chrono::steady_clock::now());

   std::thread disturber([&] {
      untilWaiting(platform);
      // Notified without the image lock, so a single notify can land in the
      // gap between the waiter counting itself and actually blocking, and be
      // lost. Keep notifying until the waiter has demonstrably come back
      // round and waited again; only then publish something real.
      while(platform.conditionWaits.load() < 2)
      {
         platform.notifyWithNoNews();
         std::this_thread::yield();
      }
      image.publish(statusAt(2), std::chrono::steady_clock::now());
   });
   const StampedStatus reached = image.sync(1, std::chrono::steady_clock::now() + std::chrono::seconds(5));
   disturber.join();

   EXPECT_EQ(reached.exchangeCount, 2u);
   EXPECT_EQ(reached.status.position, 2);
   EXPECT_GE(platform.conditionWaits.load(), 2); // At least one spurious wake was re-checked, not trusted.
}

} // namespace
} // namespace Robotiq::detail
