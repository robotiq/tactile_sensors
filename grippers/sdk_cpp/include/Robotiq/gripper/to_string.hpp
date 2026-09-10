// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Human-readable rendering of the command and status blocks —
//!        the manual's mnemonics with decoded bits, states and fault
//!        codes — for log output and interactive debugging.

#pragma once

#include <string>
#include <string_view>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/fault_status.hpp>
#include <Robotiq/gripper/status.hpp>

namespace Robotiq {

//! \ingroup gripper_status
//! \param state gSTA, as decoded by GripperStatusFlags::activationState().
//! \return The manual's mnemonic name for \p state.
[[nodiscard]] constexpr std::string_view toString(ActivationState state)
{
   switch(state)
   {
   case ActivationState::Reset:
      return "Reset";
   case ActivationState::InProgress:
      return "InProgress";
   case ActivationState::Reserved:
      return "Reserved";
   case ActivationState::Complete:
      return "Complete";
   }
   return "Unrecognized";
}

//! \ingroup gripper_status
//! \param detection gOBJ, as decoded by GripperStatusFlags::objectDetection().
//! \return The manual's mnemonic name for \p detection.
[[nodiscard]] constexpr std::string_view toString(ObjectDetection detection)
{
   switch(detection)
   {
   case ObjectDetection::Moving:
      return "Moving";
   case ObjectDetection::DetectedWhileOpening:
      return "DetectedWhileOpening";
   case ObjectDetection::DetectedWhileClosing:
      return "DetectedWhileClosing";
   case ObjectDetection::AtRequestedPosition:
      return "AtRequestedPosition";
   }
   return "Unrecognized";
}

//! \ingroup fault_status
//! \param fault gFLT, from FaultStatus::gripperFault().
//! \return The manual's mnemonic name for \p fault.
[[nodiscard]] constexpr std::string_view toString(GripperFault fault)
{
   switch(fault)
   {
   case GripperFault::None:
      return "None";
   case GripperFault::ActionDelayed:
      return "ActionDelayed";
   case GripperFault::ActivationRequired:
      return "ActivationRequired";
   case GripperFault::OverTemperature:
      return "OverTemperature";
   case GripperFault::NoCommunication:
      return "NoCommunication";
   case GripperFault::UnderVoltage:
      return "UnderVoltage";
   case GripperFault::AutomaticReleaseInProgress:
      return "AutomaticReleaseInProgress";
   case GripperFault::InternalFault:
      return "InternalFault";
   case GripperFault::ActivationFault:
      return "ActivationFault";
   case GripperFault::Overcurrent:
      return "Overcurrent";
   case GripperFault::AutomaticReleaseComplete:
      return "AutomaticReleaseComplete";
   }
   return "Unrecognized";
}

//! \ingroup fault_status
//! \param fault kFLT, from FaultStatus::controllerFault().
//! \return The manual's mnemonic name for \p fault.
[[nodiscard]] constexpr std::string_view toString(ControllerFault fault)
{
   switch(fault)
   {
   case ControllerFault::None:
      return "None";
   case ControllerFault::Supply24VNotDetected:
      return "Supply24VNotDetected";
   case ControllerFault::NoDeviceDetected:
      return "NoDeviceDetected";
   case ControllerFault::CommunicationNotReady:
      return "CommunicationNotReady";
   case ControllerFault::EmergencyStop:
      return "EmergencyStop";
   case ControllerFault::Overcurrent:
      return "Overcurrent";
   }
   return "Unrecognized";
}

//! \ingroup command
//! \param command The command block to render.
//! \return \p command as the manual's mnemonics, one field per byte
//!         (rACT, rGTO, ... rPR, rSP, rFR), decoded bits and all.
[[nodiscard]] std::string toString(const GripperCommand& command);

//! \ingroup gripper_status
//! \param flags The packed GRIPPER STATUS byte to render.
//! \return \p flags as the manual's mnemonics (gACT, gGTO, gSTA, gOBJ),
//!         with gSTA/gOBJ decoded to their named states.
[[nodiscard]] std::string toString(GripperStatusFlags flags);

//! \ingroup fault_status
//! \param fault The packed FAULT STATUS byte to render.
//! \return \p fault as the manual's mnemonics (gFLT, kFLT), each decoded
//!         to its named fault.
[[nodiscard]] std::string toString(FaultStatus fault);

//! \ingroup status
//! \param status The status block to render.
//! \return \p status as the manual's mnemonics, one field per byte
//!         (gACT, gGTO, ... gPR, gPO, gCU), decoded bits and all.
[[nodiscard]] std::string toString(const GripperStatus& status);

} // namespace Robotiq
