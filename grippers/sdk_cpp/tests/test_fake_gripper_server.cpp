// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/fault_status.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/detail/gripper_modbus_client.hpp>
#include <Robotiq/detail/modbus_constants.hpp>

#include "fake/status_writer.hpp"
#include "fake_gripper_fixture.hpp"
#include "test_utils.hpp"

namespace Robotiq::test {

namespace {
namespace mc = Robotiq::detail::modbus_constants;

// A command that raises rACT only — the activation request.
GripperCommand activateCommand()
{
   GripperCommand command;
   command.action.set(ActionRequestBit::Activate, true);
   return command;
}
} // namespace

class TestFakeGripperServer : public ::testing::Test
{
protected:
   TestFakeGripperServer()
      : client(std::make_unique<fake::GripperSerial>(fakeServer.server), kSlaveAddress, std::make_shared<NullLogger>())
   {
   }

   InstrumentedFakeGripperServer fakeServer;
   detail::GripperModbusClient client;
};

TEST_F(TestFakeGripperServer, status_block_reads_back_initial_zeros)
{
   const auto status = client.readStatus();
   EXPECT_EQ(status.gripperStatus.raw(), 0);
   EXPECT_EQ(status.faultStatus.raw(), 0);
   EXPECT_EQ(status.positionRequestEcho, 0);
   EXPECT_EQ(status.position, 0);
   EXPECT_EQ(status.current, 0);
}

TEST_F(TestFakeGripperServer, command_block_writes_land_in_the_registers)
{
   GripperCommand command = GripperCommand::defaults(); // set rGTO to move
   command.action.set(ActionRequestBit::GoTo, true);
   command.positionRequest = 0xFF;
   command.force = 0x80;
   client.writeCommand(command);

   EXPECT_EQ(fakeServer.model.at(mc::kCommandAddress), 0x0900);
   EXPECT_EQ(fakeServer.model.at(mc::kCommandAddress + 1), 0x00FF);
   EXPECT_EQ(fakeServer.model.at(mc::kCommandAddress + 2), 0xFF80);
}

TEST_F(TestFakeGripperServer, activation_request_completes)
{
   // Instant completion is a harness simplification: the real gripper
   // sweeps for ~1 s and reports gSTA InProgress when at the closing instant.
   client.writeCommand(activateCommand());

   const auto status = client.readStatus();

   EXPECT_TRUE(status.gripperStatus.activated());
   EXPECT_EQ(status.gripperStatus.activationState(), ActivationState::Complete);
}

TEST_F(TestFakeGripperServer, pre_seeded_activation_survives_without_a_rising_edge)
{
   // A gripper that retained activation from a previous session reports
   // Complete on the first read, with no command written at all.
   fakeServer.model.setActivated();

   const auto status = client.readStatus();

   EXPECT_TRUE(status.gripperStatus.activated());
   EXPECT_EQ(status.gripperStatus.activationState(), ActivationState::Complete);
}

TEST_F(TestFakeGripperServer, gripper_fault_latches_across_reads)
{
   fakeServer.model.setFault(GripperFault::UnderVoltage);
   client.writeCommand(activateCommand()); // rACT stays set: the fault holds

   EXPECT_EQ(client.readStatus().faultStatus.gripperFault(), GripperFault::UnderVoltage);
   EXPECT_EQ(client.readStatus().faultStatus.gripperFault(), GripperFault::UnderVoltage);
}

TEST_F(TestFakeGripperServer, clearing_the_activate_bit_clears_the_fault_and_counts_a_reset)
{
   fakeServer.model.setFault(GripperFault::UnderVoltage);
   client.writeCommand(activateCommand());
   ASSERT_EQ(client.readStatus().faultStatus.gripperFault(), GripperFault::UnderVoltage);

   client.writeCommand(GripperCommand{}); // rACT falling edge: the reset request
   const auto status = client.readStatus();

   EXPECT_EQ(status.faultStatus.gripperFault(), GripperFault::None);
   EXPECT_FALSE(status.gripperStatus.activated());
   EXPECT_EQ(fakeServer.model.resets.load(), 1);
}

TEST_F(TestFakeGripperServer, resets_counts_falling_edges_only)
{
   client.writeCommand(activateCommand());
   client.writeCommand(activateCommand()); // rACT held: not an edge
   EXPECT_EQ(fakeServer.model.resets.load(), 0);

   client.writeCommand(GripperCommand{});
   client.writeCommand(GripperCommand{}); // rACT stays clear: not an edge
   EXPECT_EQ(fakeServer.model.resets.load(), 1);

   client.writeCommand(activateCommand());
   client.writeCommand(GripperCommand{});
   EXPECT_EQ(fakeServer.model.resets.load(), 2);
}

TEST_F(TestFakeGripperServer, pinned_status_still_tracks_the_activate_edge)
{
   // The pin simulates a gripper stuck mid-activation: rACT is echoed but the
   // sweep never completes. The whole block is pinned, by design.
   GripperStatus stuck;
   fake::setActivated(stuck, true);
   fake::setActivationState(stuck, ActivationState::InProgress);
   fakeServer.model.pinnedStatus = stuck;

   client.writeCommand(activateCommand());
   EXPECT_EQ(client.readStatus().gripperStatus.activationState(), ActivationState::InProgress);

   // Releasing the pin must not lose the rACT edge tracked while pinned: the
   // falling edge below is a reset request, and goes uncounted if the pinned
   // path stops following rACT.
   fakeServer.model.pinnedStatus.reset();
   client.writeCommand(GripperCommand{});

   EXPECT_EQ(fakeServer.model.resets.load(), 1);
   EXPECT_FALSE(client.readStatus().gripperStatus.activated());
}

TEST_F(TestFakeGripperServer, transaction_counters_track_the_bus_traffic)
{
   client.writeCommand(activateCommand());
   (void)client.readStatus();
   (void)client.readStatus();
   (void)client.exchange(activateCommand()); // FC 0x17 is both a write and a read

   EXPECT_EQ(fakeServer.model.commandWrites.load(), 2);
   EXPECT_EQ(fakeServer.model.statusReads.load(), 3);
}

TEST_F(TestFakeGripperServer, refuses_a_range_outside_the_register_map)
{
   // Driven at the server directly: the client only ever asks for the two
   // documented blocks, so nothing above reaches this path.
   const uint16_t outside = fake::RegisterModel::kRegisterCount;
   fakeServer.server.deliver(readHoldingRegistersFrame(kSlaveAddress, outside, 1));

   // FC 0x03 | 0x80 marks an exception response, and 0x02 is illegal data
   // address.
   const std::vector<uint8_t> expected = withCrc({kSlaveAddress, 0x83, 0x02});
   EXPECT_EQ(fakeServer.server.drain(expected.size()), expected);
   EXPECT_TRUE(fakeServer.server.drain(1).empty()) << "the server answered with more than the exception response";
}

TEST(RegisterModelRange, covers_the_whole_register_file_and_nothing_past_it)
{
   using fake::RegisterModel;
   EXPECT_TRUE(RegisterModel::containsRange(0, 1));
   EXPECT_TRUE(RegisterModel::containsRange(mc::kStatusAddress, RegisterModel::kBlockRegisters));
   EXPECT_FALSE(RegisterModel::containsRange(RegisterModel::kRegisterCount, 1));
   EXPECT_FALSE(RegisterModel::containsRange(mc::kStatusAddress, RegisterModel::kBlockRegisters + 1));
}

TEST_F(TestFakeGripperServer, position_echo_and_current_follow_the_request)
{
   GripperCommand command = GripperCommand::defaults();
   command.action.set(ActionRequestBit::GoTo, true);
   command.positionRequest = 0x7F;
   client.writeCommand(command);

   const auto status = client.readStatus();

   EXPECT_EQ(status.positionRequestEcho, 0x7F);
   EXPECT_EQ(status.position, 0x7F); // the fake's fingers arrive instantly
   EXPECT_EQ(status.current, fake::RegisterModel::kReportedCurrent);
}
} // namespace Robotiq::test
