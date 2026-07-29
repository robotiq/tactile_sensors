// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/fault_status.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/register_map.hpp>
#include <Robotiq/detail/gripper_modbus_client.hpp>
#include <Robotiq/detail/modbus_constants.hpp>

#include "fake_gripper.hpp"
#include "test_utils.hpp"

namespace Robotiq::test {

namespace {
namespace mc = Robotiq::detail::modbus_constants;
namespace rm = Robotiq::register_map;

// A command that raises rACT only — the activation request.
GripperCommand activateCommand()
{
   GripperCommand command;
   command.action.set(ActionRequestBit::Activate, true);
   return command;
}
} // namespace

class FakeGripperTest : public ::testing::Test
{
protected:
   FakeGripperTest()
      : client(std::make_unique<FakeGripperSerial>(gripper), kSlaveAddress, std::make_shared<NullLogger>())
   {
   }

   FakeGripperModbusServer gripper;
   detail::GripperModbusClient client;
};

TEST_F(FakeGripperTest, status_block_reads_back_initial_zeros)
{
   const auto status = client.readStatus();
   EXPECT_EQ(status.gripperStatus.raw(), 0);
   EXPECT_EQ(status.faultStatus.raw(), 0);
   EXPECT_EQ(status.positionRequestEcho, 0);
   EXPECT_EQ(status.position, 0);
   EXPECT_EQ(status.current, 0);
}

TEST_F(FakeGripperTest, command_block_writes_land_in_the_registers)
{
   GripperCommand command = GripperCommand::defaults(); // set rGTO to move
   command.action.set(ActionRequestBit::GoTo, true);
   command.positionRequest = 0xFF;
   command.force = 0x80;
   client.writeCommand(command);

   EXPECT_EQ(gripper.registers[mc::kCommandAddress], 0x0900);
   EXPECT_EQ(gripper.registers[mc::kCommandAddress + 1], 0x00FF);
   EXPECT_EQ(gripper.registers[mc::kCommandAddress + 2], 0xFF80);
}

TEST_F(FakeGripperTest, activation_request_completes)
{
   // Instant completion is a harness simplification: the real gripper
   // sweeps for ~1 s and reports gSTA InProgress when at the closing instant.
   client.writeCommand(activateCommand());

   const auto status = client.readStatus();
   const auto statusByte = status.gripperStatus.raw();

   EXPECT_NE(statusByte & rm::kActivationStatusMask, 0);
   EXPECT_EQ((statusByte & rm::kActivationStateMask) >> rm::kActivationStateShift, rm::kActivationStateComplete);
}

TEST_F(FakeGripperTest, pre_seeded_activation_survives_without_a_rising_edge)
{
   // A gripper that retained activation from a previous session reports
   // Complete on the first read, with no command written at all.
   gripper.givenGripperIsActivated();

   const auto status = client.readStatus();

   EXPECT_TRUE(status.gripperStatus.activated());
   EXPECT_EQ(status.gripperStatus.activationState(), ActivationState::Complete);
}

TEST_F(FakeGripperTest, gripper_fault_latches_across_reads)
{
   gripper.givenGripperFault(static_cast<uint8_t>(GripperFault::UnderVoltage));
   client.writeCommand(activateCommand()); // rACT stays set: the fault holds

   // intentionally read twice:
   EXPECT_EQ(client.readStatus().faultStatus.gripperFault(), GripperFault::UnderVoltage);
   EXPECT_EQ(client.readStatus().faultStatus.gripperFault(), GripperFault::UnderVoltage);
}

TEST_F(FakeGripperTest, clearing_the_activate_bit_clears_the_fault_and_counts_a_reset)
{
   gripper.givenGripperFault(static_cast<uint8_t>(GripperFault::UnderVoltage));
   client.writeCommand(activateCommand());
   ASSERT_EQ(client.readStatus().faultStatus.gripperFault(), GripperFault::UnderVoltage);

   client.writeCommand(GripperCommand{}); // rACT falling edge: the reset request
   const auto status = client.readStatus();

   EXPECT_EQ(status.faultStatus.gripperFault(), GripperFault::None);
   EXPECT_FALSE(status.gripperStatus.activated());
   EXPECT_EQ(gripper.resets.load(), 1);
}

TEST_F(FakeGripperTest, resets_counts_falling_edges_only)
{
   client.writeCommand(activateCommand());
   client.writeCommand(activateCommand()); // rACT held: not an edge
   EXPECT_EQ(gripper.resets.load(), 0);

   client.writeCommand(GripperCommand{});
   client.writeCommand(GripperCommand{}); // rACT stays clear: not an edge
   EXPECT_EQ(gripper.resets.load(), 1);

   client.writeCommand(activateCommand());
   client.writeCommand(GripperCommand{});
   EXPECT_EQ(gripper.resets.load(), 2);
}

TEST_F(FakeGripperTest, forced_status_byte_pins_byte_zero_only)
{
   // The knob simulates a gripper stuck mid-activation: rACT is echoed but
   // the sweep never completes.
   gripper.forcedStatusByte =
      static_cast<uint8_t>(rm::kActivationStatusMask | (rm::kActivationStateInProgress << rm::kActivationStateShift));
   GripperCommand command = activateCommand();
   command.action.set(ActionRequestBit::GoTo, true);
   command.positionRequest = 0x40;
   client.writeCommand(command);

   const auto status = client.readStatus();
   EXPECT_EQ(status.gripperStatus.activationState(), ActivationState::InProgress);
   EXPECT_EQ(status.positionRequestEcho, 0x40); // the rest of the block is not pinned
   EXPECT_EQ(status.position, 0x40);
   EXPECT_EQ(status.current, FakeGripperModbusServer::kSimulatedCurrent);

   // The rACT edge is tracked while pinned, so the falling edge below still
   // counts as a reset request.
   gripper.forcedStatusByte.reset();
   client.writeCommand(GripperCommand{});

   EXPECT_EQ(gripper.resets.load(), 1);
   EXPECT_FALSE(client.readStatus().gripperStatus.activated());
}

TEST_F(FakeGripperTest, transaction_counters_track_the_bus_traffic)
{
   client.writeCommand(activateCommand());
   (void)client.readStatus();
   (void)client.readStatus();
   (void)client.exchange(activateCommand()); // FC 0x17 is both a write and a read

   EXPECT_EQ(gripper.commandWrites.load(), 2);
   EXPECT_EQ(gripper.statusReads.load(), 3);
}

TEST_F(FakeGripperTest, position_echo_and_current_follow_the_request)
{
   GripperCommand command = GripperCommand::defaults();
   command.action.set(ActionRequestBit::GoTo, true);
   command.positionRequest = 0x7F;
   client.writeCommand(command);

   const auto status = client.readStatus();

   EXPECT_EQ(status.positionRequestEcho, 0x7F);
   EXPECT_EQ(status.position, 0x7F); // the fake's fingers arrive instantly
   EXPECT_EQ(status.current, FakeGripperModbusServer::kSimulatedCurrent);
}
} // namespace Robotiq::test
