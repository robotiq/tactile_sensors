// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/status.hpp>

#include "process_image.hpp"

namespace Robotiq::detail {
namespace {
using namespace std::chrono_literals;

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

TEST_F(TestProcessImage, waking_with_nobody_waiting_is_harmless)
{
   image.wakeAll();
   image.publish(statusAt(3), t0);
   EXPECT_EQ(image.stampedStatus().exchangeCount, 1u);
}

} // namespace
} // namespace Robotiq::detail
