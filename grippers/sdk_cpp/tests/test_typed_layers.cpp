// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <type_traits>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/fault_status.hpp>
#include <Robotiq/gripper/status.hpp>

namespace Robotiq::test {

namespace {
GripperCommand closeCommand()
{
   GripperCommand command = GripperCommand::defaults();
   command.action.set(ActionRequestBit::GoTo, true);
   command.positionRequest = 0xFF;
   command.speed = 0xFF;
   command.force = 0x80;
   return command;
}
} // namespace

TEST(TestGripperCommand, defaults_is_activated_but_motionless)
{
   const GripperCommand command = GripperCommand::defaults();
   EXPECT_EQ(command.action.value(), 0x01); // rACT set, rGTO clear: no motion
   EXPECT_EQ(command.positionRequest, 0x00);
   EXPECT_EQ(command.speed, 0xFF);
   EXPECT_EQ(command.force, 0xFF);
}

TEST(TestGripperBlocks, equality_compares_the_full_block)
{
   const GripperCommand command = GripperCommand::defaults();
   GripperCommand other = command;
   EXPECT_EQ(command, other);
   other.reserved[3] = 1; // reserved bytes count too: the block is the value
   EXPECT_NE(command, other);

   GripperStatus status;
   GripperStatus same;
   EXPECT_EQ(status, same);
   EXPECT_EQ(status.gripperStatus, same.gripperStatus);
   EXPECT_EQ(status.faultStatus, same.faultStatus);
}

TEST(TestGripperCommand, lays_out_the_documented_bytes)
{
   const GripperCommand command = closeCommand();
   const uint8_t* bytes = command.data();
   EXPECT_EQ(bytes[0], 0x09); // ACTION REQUEST: rACT | rGTO
   EXPECT_EQ(bytes[3], 0xFF); // POSITION REQUEST
   EXPECT_EQ(bytes[4], 0xFF); // SPEED
   EXPECT_EQ(bytes[5], 0x80); // FORCE
}

TEST(TestActionRequest, get_and_set_track_the_named_bits)
{
   ActionRequest action; // empty: all bits clear
   EXPECT_FALSE(action.get(ActionRequestBit::Activate));

   action.set(ActionRequestBit::Activate);
   action.set(ActionRequestBit::GoTo);
   EXPECT_TRUE(action.get(ActionRequestBit::Activate));
   EXPECT_TRUE(action.get(ActionRequestBit::GoTo));
   EXPECT_EQ(action.value(), 0x09);

   action.set(ActionRequestBit::GoTo, false);
   EXPECT_TRUE(action.get(ActionRequestBit::Activate)); // setting one bit leaves the others intact
   EXPECT_FALSE(action.get(ActionRequestBit::GoTo));
}

TEST(TestGripperStatus, decodes_all_documented_fields)
{
   // byte 0 = 0x31 (gACT | gSTA complete); byte 2 fault 0x09; byte 3 gPR
   // 0xFF; byte 4 gPO 0xE6; byte 5 gCU 0x14.
   GripperStatus status;
   const std::array<uint8_t, 6> bytes = {0x31, 0x00, 0x09, 0xFF, 0xE6, 0x14};
   std::memcpy(status.data(), bytes.data(), bytes.size());

   EXPECT_TRUE(status.gripperStatus.activated());
   EXPECT_FALSE(status.gripperStatus.goToEnabled());
   EXPECT_EQ(status.gripperStatus.activationState(), ActivationState::Complete);
   EXPECT_EQ(status.gripperStatus.objectDetection(), ObjectDetection::Moving);
   EXPECT_EQ(status.faultStatus.gripperFault(), GripperFault::NoCommunication);
   EXPECT_EQ(status.faultStatus.controllerFault(), ControllerFault::None);
   EXPECT_EQ(status.positionRequestEcho, 0xFF);
   EXPECT_EQ(status.position, 0xE6);
   EXPECT_EQ(status.current, 0x14);
}

TEST(TestActionRequest, bits_round_trip_for_all_combinations)
{
   constexpr ActionRequestBit kBits[] = {ActionRequestBit::Activate,
                                         ActionRequestBit::GoTo,
                                         ActionRequestBit::AutoRelease,
                                         ActionRequestBit::AutoReleaseOpenDirection};
   for(int i = 0; i < 16; ++i)
   {
      GripperCommand command;
      std::memset(command.data(), 0, command.size());
      for(int b = 0; b < 4; ++b)
      {
         command.action.set(kBits[b], (i & (1 << b)) != 0);
      }

      GripperCommand decoded;
      std::memcpy(decoded.data(), command.data(), command.size());

      for(int b = 0; b < 4; ++b)
      {
         EXPECT_EQ(decoded.action.get(kBits[b]), command.action.get(kBits[b])) << i << ':' << b;
      }
   }
}

TEST(TestFaultStatus, splits_into_gripper_and_controller_nibbles)
{
   GripperStatus status;
   status.data()[2] = 0x5C; // kFLT = 0x5 (controller), gFLT = 0xC (gripper)
   EXPECT_EQ(status.faultStatus.raw(), 0x5C);
   EXPECT_EQ(status.faultStatus.gripperFault(), GripperFault::InternalFault);
   EXPECT_EQ(status.faultStatus.controllerFault(), ControllerFault::NoDeviceDetected);
}

TEST(TestFaultStatus, severity_matches_the_documented_tiers)
{
   EXPECT_EQ(severity(GripperFault::None), FaultSeverity::None);
   EXPECT_EQ(severity(GripperFault::ActionDelayed), FaultSeverity::Warning);
   EXPECT_EQ(severity(GripperFault::OverTemperature), FaultSeverity::Minor);
   EXPECT_EQ(severity(GripperFault::UnderVoltage), FaultSeverity::Major);
   EXPECT_EQ(severity(ControllerFault::NoDeviceDetected), FaultSeverity::Warning);
   EXPECT_EQ(severity(ControllerFault::CommunicationNotReady), FaultSeverity::Minor);
   EXPECT_EQ(severity(ControllerFault::EmergencyStop), FaultSeverity::Major);
}

TEST(TestFaultStatus, undocumented_code_is_carried_through_and_treated_as_major)
{
   GripperStatus status;
   status.data()[2] = 0x01; // 0x01 is not a documented gFLT code
   EXPECT_EQ(status.faultStatus.raw(), 0x01);
   EXPECT_NE(status.faultStatus.gripperFault(), GripperFault::None);
   EXPECT_EQ(severity(status.faultStatus.gripperFault()), FaultSeverity::Major);
}

TEST(TestGripperBlocks, overlay_the_full_physical_block)
{
   static_assert(sizeof(GripperCommand) == 16);
   static_assert(sizeof(GripperStatus) == 16);
   static_assert(std::is_standard_layout_v<GripperCommand>);
   static_assert(std::is_standard_layout_v<GripperStatus>);
   static_assert(std::is_trivially_copyable_v<GripperCommand>);
   static_assert(std::is_trivially_copyable_v<GripperStatus>);
   EXPECT_EQ(GripperCommand{}.size(), 16u);
}
} // namespace Robotiq::test
