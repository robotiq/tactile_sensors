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

[[nodiscard]] std::string toString(const GripperCommand& command);

[[nodiscard]] std::string toString(GripperStatusFlags flags);

[[nodiscard]] std::string toString(FaultStatus fault);

[[nodiscard]] std::string toString(const GripperStatus& status);

} // namespace Robotiq
