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

TEST(TestGripperCommand, default_construction_is_all_zero)
{
   // The safety property behind default construction: a stray default
   // never activates or moves the gripper.
   const GripperCommand command;
   const std::array<uint8_t, 16> zeros{};
   EXPECT_EQ(std::memcmp(command.data(), zeros.data(), command.size()), 0);
}

TEST(TestGripperBlocks, equality_compares_the_full_block)
{
   const GripperCommand command = GripperCommand::defaults();
   GripperCommand other = command;
   EXPECT_EQ(command, other);
   other.reservedTail[3] = 1; // reserved bytes count too: the block is the value
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

TEST(TestGripperCommand, emergency_release_bits_land_where_the_manual_says)
{
   GripperCommand release;
   release.action.set(ActionRequestBit::AutoRelease);
   EXPECT_EQ(release.data()[0], 0x10); // rATR
   release.action.set(ActionRequestBit::AutoReleaseOpenDirection);
   EXPECT_EQ(release.data()[0], 0x30); // rATR | rARD
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

   action.set(ActionRequestBit::GoTo);
   action.unset(ActionRequestBit::GoTo);
   EXPECT_EQ(action.value(), 0x01);
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

TEST(TestGripperStatus, decodes_every_gsta_and_gobj_pattern)
{
   struct Case
   {
      uint8_t raw;
      ActivationState state;
      ObjectDetection object;
   };
   // Every value the two-bit gSTA and gOBJ fields can hold, including the
   // gSTA pattern the manual leaves unallocated.
   constexpr Case kCases[] = {
      {0x00, ActivationState::Reset, ObjectDetection::Moving},
      {0x10, ActivationState::InProgress, ObjectDetection::Moving},
      {0x20, ActivationState::Reserved, ObjectDetection::Moving},
      {0x30, ActivationState::Complete, ObjectDetection::Moving},
      {0x40, ActivationState::Reset, ObjectDetection::DetectedWhileOpening},
      {0x80, ActivationState::Reset, ObjectDetection::DetectedWhileClosing},
      {0xC0, ActivationState::Reset, ObjectDetection::AtRequestedPosition},
      {0xD1, ActivationState::InProgress, ObjectDetection::AtRequestedPosition},
      {0xF9, ActivationState::Complete, ObjectDetection::AtRequestedPosition},
   };

   for(const Case& testCase : kCases)
   {
      const GripperStatusFlags flags = GripperStatusFlags::fromRaw(testCase.raw);
      EXPECT_EQ(flags.activationState(), testCase.state) << "raw " << static_cast<int>(testCase.raw);
      EXPECT_EQ(flags.objectDetection(), testCase.object) << "raw " << static_cast<int>(testCase.raw);
      EXPECT_EQ(flags.activated(), (testCase.raw & 0x01) != 0) << "raw " << static_cast<int>(testCase.raw);
      EXPECT_EQ(flags.goToEnabled(), (testCase.raw & 0x08) != 0) << "raw " << static_cast<int>(testCase.raw);
   }
}

TEST(TestActionRequest, bits_round_trip_for_all_combinations)
{
   constexpr ActionRequestBit kBits[] = {ActionRequestBit::Activate,
                                         ActionRequestBit::GoTo,
                                         ActionRequestBit::AutoRelease,
                                         ActionRequestBit::AutoReleaseOpenDirection};
   for(int i = 0; i < 16; ++i)
   {
      GripperCommand command; // all-zero, per default_construction_is_all_zero
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
   // kFLT = 0x5 (controller), gFLT = 0xC (gripper)
   constexpr FaultStatus fault = FaultStatus::fromRaw(0x5C);
   EXPECT_EQ(fault.raw(), 0x5C);
   EXPECT_EQ(fault.gripperFault(), GripperFault::InternalFault);
   EXPECT_EQ(fault.controllerFault(), ControllerFault::NoDeviceDetected);
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
   constexpr FaultStatus fault = FaultStatus::fromRaw(0x01); // 0x01 is not a documented gFLT code
   EXPECT_EQ(fault.raw(), 0x01);
   EXPECT_NE(fault.gripperFault(), GripperFault::None);
   EXPECT_EQ(severity(fault.gripperFault()), FaultSeverity::Major);
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
