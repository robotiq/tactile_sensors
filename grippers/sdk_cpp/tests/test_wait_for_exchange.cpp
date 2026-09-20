// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/wait.hpp>
#include <Robotiq/gripper/fault_status.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/stamped_exchange.hpp>
#include <Robotiq/gripper/status.hpp>

#include "fake_gripper_fixture.hpp"
#include "gripper_test_helpers.hpp"
#include "instrumented_platform.hpp"

namespace Robotiq::test {
namespace {
constexpr std::chrono::seconds kWait{2};

// Returned as a prvalue: Gripper is neither copyable nor movable, and C++17's
// guaranteed elision is what lets a factory hand one back by value anyway.
Gripper makeGripper(InstrumentedFakeGripperServer& fakeServer,
                    std::chrono::milliseconds period,
                    std::shared_ptr<Platform> platform = makeDefaultPlatform())
{
   return Gripper(std::make_unique<fake::GripperSerial>(fakeServer.server),
                  kSlaveAddress,
                  period,
                  std::move(platform),
                  std::make_shared<NullLogger>());
}

//! A gripper whose link has already failed for good: no exchange will ever
//! complete again, so nothing but the stop can end a wait on it.
std::unique_ptr<Gripper> makeStalledGripper(InstrumentedFakeGripperServer& fakeServer)
{
   auto serial = std::make_unique<WriteFailingSerial>(fakeServer.server);
   WriteFailingSerial& link = *serial;
   auto gripper = std::make_unique<Gripper>(std::move(serial),
                                            kSlaveAddress,
                                            kFastPeriod,
                                            makeDefaultPlatform(),
                                            std::make_shared<NullLogger>());
   link.failing.store(true);
   EXPECT_TRUE(Robotiq::waitFor([&] { return gripper->connectionState() == ConnectionState::Faulted; },
                                kWait,
                                std::chrono::milliseconds(1)));
   return gripper;
}

class TestWaitForExchange : public ::testing::Test
{
protected:
   InstrumentedFakeGripperServer fakeServer;
   Gripper gripper = makeGripper(fakeServer, kSyncPeriod);
};

TEST_F(TestWaitForExchange, a_wait_for_a_count_returns_an_exchange_that_reached_it)
{
   // Activated first: the fake, like the gripper, only latches a fault
   // while rACT is set.
   ASSERT_EQ(activate(gripper, kWait), ActivationResult::Activated);
   const std::optional<StampedExchange> before = gripper.waitForExchange(kWait);
   ASSERT_TRUE(before.has_value());
   ASSERT_EQ(before->status.faultStatus.gripperFault(), GripperFault::None);

   fakeServer.model.setFault(GripperFault::Overcurrent);
   ASSERT_TRUE(
      Robotiq::waitFor([&] { return gripper.getStatus().faultStatus.gripperFault() == GripperFault::Overcurrent; },
                       kWait,
                       std::chrono::milliseconds(1)));

   // Asking for one past the count acted on returns the exchange that carries it.
   const std::optional<StampedExchange> after = gripper.waitForExchangeCount(before->metadata.exchangeCount + 1, kWait);
   ASSERT_TRUE(after.has_value());
   EXPECT_EQ(after->status.faultStatus.gripperFault(), GripperFault::Overcurrent);
   EXPECT_GT(after->metadata.exchangeCount, before->metadata.exchangeCount);
}

TEST_F(TestWaitForExchange, timestamps_advance_with_the_cycle_count)
{
   std::optional<StampedExchange> previous = gripper.waitForExchange(kWait);
   ASSERT_TRUE(previous.has_value());
   for(int wake = 0; wake < 5; ++wake)
   {
      const std::optional<StampedExchange> stamped =
         gripper.waitForExchangeCount(previous->metadata.exchangeCount + 1, kWait);
      ASSERT_TRUE(stamped.has_value()) << "wake " << wake;
      EXPECT_GT(stamped->metadata.exchangeCount, previous->metadata.exchangeCount) << "wake " << wake;
      EXPECT_GT(stamped->metadata.timestamp, previous->metadata.timestamp) << "wake " << wake;
      previous = stamped;
   }
}

TEST_F(TestWaitForExchange, the_most_recent_exchange_has_a_usable_timestamp_from_the_start)
{
   // The seeding read stamps the first record, not a zero clock: a derived
   // parameter seeded from it starts with a usable interval.
   const StampedExchange stamped = gripper.getMostRecentStampedExchange();
   EXPECT_GT(stamped.metadata.timestamp, std::chrono::steady_clock::time_point{});
   EXPECT_LE(stamped.metadata.timestamp, std::chrono::steady_clock::now());
}

TEST_F(TestWaitForExchange, the_most_recent_exchange_is_never_behind_the_one_a_wait_returned)
{
   const std::optional<StampedExchange> woke = gripper.waitForExchange(kWait);
   ASSERT_TRUE(woke.has_value());
   EXPECT_GE(gripper.getMostRecentStampedExchange().metadata.exchangeCount, woke->metadata.exchangeCount);
}

TEST_F(TestWaitForExchange, an_exchange_pairs_the_command_it_wrote_with_the_status_that_answered_it)
{
   ASSERT_EQ(activate(gripper, kWait), ActivationResult::Activated);
   GripperCommand command = GripperCommand::defaults();
   command.positionRequest = 42;
   gripper.setCommand(command);

   // The fake echoes a request in the status that answers its write, so the
   // first record carrying the block also carries its echo.
   std::optional<StampedExchange> stamped;
   for(int wake = 0; wake < 10; ++wake)
   {
      stamped = gripper.waitForExchange(kWait);
      ASSERT_TRUE(stamped.has_value());
      if(stamped->command.positionRequest == 42)
      {
         break;
      }
   }
   ASSERT_EQ(stamped->command.positionRequest, 42);
   EXPECT_EQ(stamped->status.positionRequestEcho, 42);
}

TEST_F(TestWaitForExchange, wait_for_returns_the_first_exchange_the_predicate_holds_for)
{
   ASSERT_EQ(activate(gripper, kWait), ActivationResult::Activated);
   GripperCommand command = GripperCommand::defaults();
   command.positionRequest = 42;
   gripper.setCommand(command);
   const uint64_t before = gripper.getMostRecentStampedExchange().metadata.exchangeCount;

   int evaluated = 0;
   const std::optional<StampedExchange> echoed = waitFor(
      gripper,
      [&](const StampedExchange& exchange) {
         ++evaluated;
         return exchange.status.positionRequestEcho == 42;
      },
      kWait);
   ASSERT_TRUE(echoed.has_value());
   EXPECT_EQ(echoed->status.positionRequestEcho, 42);
   EXPECT_EQ(echoed->command.positionRequest, 42);
   // At most one evaluation per exchange; fewer when the test thread fell
   // behind the cycle and was handed the newest.
   EXPECT_LE(static_cast<uint64_t>(evaluated), echoed->metadata.exchangeCount - before);
}

TEST_F(TestWaitForExchange, wait_for_hands_the_predicate_each_exchange_once)
{
   std::optional<uint64_t> previous;
   const std::optional<StampedExchange> fifth = waitFor(
      gripper,
      [&](const StampedExchange& exchange) {
         if(previous)
         {
            // Never the same exchange twice, never backwards; not always
            // +1, since a thread that fell behind is handed the newest.
            EXPECT_GT(exchange.metadata.exchangeCount, *previous);
         }
         previous = exchange.metadata.exchangeCount;
         return exchange.metadata.exchangeCount >= 5;
      },
      kWait);
   ASSERT_TRUE(fifth.has_value());
   EXPECT_EQ(fifth->metadata.exchangeCount, *previous);
}

TEST_F(TestWaitForExchange, the_largest_timeout_waits_for_the_cycle_instead_of_expiring_at_once)
{
   EXPECT_TRUE(gripper.waitForExchange(std::chrono::milliseconds::max()).has_value());
}

TEST(TestWaitForExchangePlatform, waits_on_the_platforms_condition_variable_not_a_sleep)
{
   InstrumentedFakeGripperServer fakeServer;
   const auto platform = std::make_shared<InstrumentedPlatform>();
   const Gripper gripper = makeGripper(fakeServer, kSyncPeriod, platform);

   // Twice: the first publish may land before the first wait even starts.
   ASSERT_TRUE(gripper.waitForExchange(kWait).has_value());
   ASSERT_TRUE(gripper.waitForExchange(kWait).has_value());
   EXPECT_GT(platform->conditionWaits.load(), 0);
   // A native condition variable blocks; a wait never falls back to sleeping.
   EXPECT_EQ(platform->sleepFors.load(), 0);
}

TEST(TestWaitForExchangePlatform, a_control_loop_busy_acting_on_an_exchange_never_holds_the_cycle_up)
{
   InstrumentedFakeGripperServer fakeServer;
   const Gripper gripper = makeGripper(fakeServer, kFastPeriod);

   std::atomic<bool> busy{false};
   std::atomic<bool> release{false};
   std::atomic<uint64_t> skippedWhileBusy{0};
   std::thread controlLoop([&] {
      const std::optional<StampedExchange> before = gripper.waitForExchange(kWait);
      // Stands in for arbitrarily slow application work on that exchange.
      busy.store(true);
      while(!release.load())
      {
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      // Back from the "work": resumes on the newest exchange at once, whose
      // count says what went by.
      const std::optional<StampedExchange> after =
         before ? gripper.waitForExchangeCount(before->metadata.exchangeCount + 1, kWait) : std::nullopt;
      if(before && after)
      {
         skippedWhileBusy.store(after->metadata.exchangeCount - before->metadata.exchangeCount - 1);
      }
   });
   // A failed assertion returns early; the loop must not outlive the
   // Gripper it waits on.
   struct Releaser
   {
      std::atomic<bool>& release;
      std::thread& loop;

      ~Releaser()
      {
         release.store(true);
         if(loop.joinable())
         {
            loop.join();
         }
      }
   } const releaser{release, controlLoop};

   ASSERT_TRUE(Robotiq::waitFor([&] { return busy.load(); }, kWait, std::chrono::milliseconds(1)));

   // That cycles keep completing is the assertion; how fast is not.
   for(int cycle = 0; cycle < 100; ++cycle)
   {
      ASSERT_TRUE(gripper.waitForExchange(kWait).has_value());
   }

   // Read after the join, which publishes the count.
   release.store(true);
   controlLoop.join();
   // Half of what went by, to stay clear of where the loop's baseline sat:
   // the point is that they are counted, not the exact count.
   EXPECT_GE(skippedWhileBusy.load(), 50u);
}

TEST(TestWaitForExchangeStall, a_stalled_bus_times_the_wait_out)
{
   InstrumentedFakeGripperServer fakeServer;
   const auto gripper = makeStalledGripper(fakeServer);

   // No exchange can complete, so the wait returns nothing — a caller sees
   // the stall here without waiting for connectionState() to degrade.
   EXPECT_FALSE(gripper->waitForExchange(std::chrono::milliseconds(30)).has_value());
}

TEST_F(TestWaitForExchange, a_block_is_carried_once_a_cycle_writes_it)
{
   GripperCommand command = gripper.getCommand();
   command.positionRequest = 42;

   const std::optional<StampedExchange> carried = setCommandAndWaitForExchange(gripper, command, kWait);
   ASSERT_TRUE(carried.has_value());
   EXPECT_EQ(carried->command, command);
   EXPECT_EQ(gripper.getCommand(), command);
   // Carried means the gripper has it, not that it is on its way.
   EXPECT_EQ(fakeServer.model.command().positionRequest, 42);
}

} // namespace
} // namespace Robotiq::test
