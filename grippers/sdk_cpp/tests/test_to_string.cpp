// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <Robotiq/gripper/to_string.hpp>

namespace Robotiq::test {

// The layout is not API, but the tests pin the exact strings so a
// change to it is deliberate rather than accidental.

TEST(TestToString, command_renders_action_bits_and_setpoints)
{
   GripperCommand command = GripperCommand::defaults();
   command.action.set(ActionRequestBit::GoTo);
   command.positionRequest = 128;
   command.force = 64;
   EXPECT_EQ(toString(command), "rACT=1 rGTO=1 rATR=0 rARD=0 rPR=128 rSP=255 rFR=64");
}

TEST(TestToString, default_command_is_all_zero)
{
   EXPECT_EQ(toString(GripperCommand{}), "rACT=0 rGTO=0 rATR=0 rARD=0 rPR=0 rSP=0 rFR=0");
}

TEST(TestToString, status_decodes_states_and_faults)
{
   GripperStatus status;
   status.gripperStatus = GripperStatusFlags::fromRaw(0xF9); // gOBJ=3 gSTA=3 gGTO gACT
   status.faultStatus = FaultStatus::fromRaw(0x09); // gFLT=NoCommunication
   status.positionRequestEcho = 255;
   status.position = 254;
   status.current = 16;
   EXPECT_EQ(toString(status),
             "gACT=1 gGTO=1 gSTA=Complete(0x3) gOBJ=AtRequestedPosition(0x3) "
             "gFLT=NoCommunication(0x9) kFLT=None(0x0) gPR=255 gPO=254 gCU=16");
}

TEST(TestToString, unrecognized_fault_code_keeps_its_nibble)
{
   // 0x6 is unallocated in the manual; 0x4 in the high nibble is the
   // controller's Supply24VNotDetected.
   const FaultStatus fault = FaultStatus::fromRaw(0x46);
   EXPECT_EQ(toString(fault), "gFLT=Unrecognized(0x6) kFLT=Supply24VNotDetected(0x4)");
}

TEST(TestToString, enum_names_are_usable_at_compile_time)
{
   static_assert(toString(ActivationState::InProgress) == "InProgress");
   static_assert(toString(ObjectDetection::DetectedWhileClosing) == "DetectedWhileClosing");
   static_assert(toString(GripperFault::ActivationRequired) == "ActivationRequired");
   static_assert(toString(ControllerFault::EmergencyStop) == "EmergencyStop");
   static_assert(toString(static_cast<GripperFault>(0x06)) == "Unrecognized");
}

} // namespace Robotiq::test
