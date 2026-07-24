// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include <nanomodbus.h>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/driver_exception.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/register_map.hpp>
#include <Robotiq/detail/gripper_modbus_client.hpp>
#include <Robotiq/detail/modbus_constants.hpp>
#include <Robotiq/detail/serial.hpp>

#include "fake_gripper.hpp"
#include <Robotiq/gripper/serial_io_exception.hpp>

namespace Robotiq::test {

namespace {
namespace mc = Robotiq::detail::modbus_constants;
namespace rm = Robotiq::register_map;

//! The gripper's slave address, from the single source of truth.
const uint8_t kSlaveAddress = ConnectionConfig().modbusSlaveAddress;

class TestFakeGripper : public ::testing::Test
{
protected:
   TestFakeGripper()
      : client(std::make_unique<FakeGripperSerial>(gripper), kSlaveAddress, std::make_shared<NullLogger>())
   {
   }

   FakeGripperModbusServer gripper;
   detail::GripperModbusClient client;
};

TEST_F(TestFakeGripper, status_block_reads_back_initial_zeros)
{
   const auto status = client.readStatus();
   EXPECT_EQ(status.gripperStatus.raw(), 0);
   EXPECT_EQ(status.faultStatus.raw(), 0);
   EXPECT_EQ(status.positionRequestEcho, 0);
   EXPECT_EQ(status.position, 0);
   EXPECT_EQ(status.current, 0);
}

TEST_F(TestFakeGripper, command_block_writes_land_in_the_registers)
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

TEST_F(TestFakeGripper, activation_request_completes)
{
   GripperCommand command;
   std::memset(command.data(), 0, command.size());
   command.action.set(ActionRequestBit::Activate, true);
   client.writeCommand(command);

   const auto status = client.readStatus();
   const auto statusByte = status.gripperStatus.raw();

   EXPECT_NE(statusByte & rm::kActivationStatusMask, 0);
   EXPECT_EQ((statusByte & rm::kActivationStateMask) >> rm::kActivationStateShift, rm::kActivationStateComplete);
}

} // namespace
} // namespace Robotiq::test
