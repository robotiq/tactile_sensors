// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The FAULT STATUS byte (byte 2 of the status block) and its two
//!        fault-code enums. The byte splits into the gripper's own fault
//!        (gFLT, low nibble) and the optional Robotiq controller's fault
//!        (kFLT, high nibble).

#pragma once

#include <cstdint>
#include <type_traits>

#include <Robotiq/gripper/register_map.hpp>

namespace Robotiq {

enum class GripperFault : uint8_t // gFLT
{
   None = 0x00,
   ActionDelayed = 0x05, // reactivation must complete before the action
   ActivationRequired = 0x07, // rACT must be set before an action
   OverTemperature = 0x08, // over max operating temperature; let it cool
   NoCommunication = 0x09, // no communication for at least 1 s
   UnderVoltage = 0x0A, // under minimum operating voltage
   AutomaticReleaseInProgress = 0x0B, // rATR emergency release running
   InternalFault = 0x0C, // contact support
   ActivationFault = 0x0D, // check for interference during activation
   Overcurrent = 0x0E,
   AutomaticReleaseComplete = 0x0F,
};

enum class ControllerFault : uint8_t // kFLT
{
   None = 0x00,
   Supply24VNotDetected = 0x04, // reconfiguration over USB still possible
   NoDeviceDetected = 0x05,
   CommunicationNotReady = 0x09, // main communication protocol booting
   EmergencyStop = 0x0C,
   Overcurrent = 0x0E, // controller overcurrent protection
};

//! The gripper manual labels the Warning tier "priority faults"; a major
//!  fault needs a reset (a rising edge on rACT) to clear.
enum class FaultSeverity : uint8_t
{
   None,
   Warning,
   Minor,
   Major,
};

// Unrecognized codes report Major so an unknown fault is never mistaken
// for harmless.
[[nodiscard]] constexpr FaultSeverity severity(GripperFault fault)
{
   switch(fault)
   {
   case GripperFault::None:
      return FaultSeverity::None;
   case GripperFault::ActionDelayed:
   case GripperFault::ActivationRequired:
      return FaultSeverity::Warning;
   case GripperFault::OverTemperature:
   case GripperFault::NoCommunication:
      return FaultSeverity::Minor;
   case GripperFault::UnderVoltage:
   case GripperFault::AutomaticReleaseInProgress:
   case GripperFault::InternalFault:
   case GripperFault::ActivationFault:
   case GripperFault::Overcurrent:
   case GripperFault::AutomaticReleaseComplete:
      return FaultSeverity::Major;
   }
   return FaultSeverity::Major;
}

[[nodiscard]] constexpr FaultSeverity severity(ControllerFault fault)
{
   switch(fault)
   {
   case ControllerFault::None:
      return FaultSeverity::None;
   case ControllerFault::Supply24VNotDetected:
   case ControllerFault::NoDeviceDetected:
      return FaultSeverity::Warning;
   case ControllerFault::CommunicationNotReady:
      return FaultSeverity::Minor;
   case ControllerFault::EmergencyStop:
   case ControllerFault::Overcurrent:
      return FaultSeverity::Major;
   }
   return FaultSeverity::Major;
}

class FaultStatus
{
public:
   [[nodiscard]] GripperFault gripperFault() const
   {
      return static_cast<GripperFault>(_bits & register_map::kGripperFaultMask);
   }
   [[nodiscard]] ControllerFault controllerFault() const
   {
      return static_cast<ControllerFault>((_bits & register_map::kControllerFaultMask)
                                          >> register_map::kControllerFaultShift);
   }

   [[nodiscard]] uint8_t raw() const { return _bits; }

private:
   uint8_t _bits = 0;
};

static_assert(std::is_standard_layout_v<FaultStatus> && std::is_trivially_copyable_v<FaultStatus>
                 && sizeof(FaultStatus) == 1,
              "FaultStatus must be a single byte");
} // namespace Robotiq
