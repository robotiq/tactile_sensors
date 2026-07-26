// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The gripper status block (gripper -> host), laid out byte for
//!        byte as the instruction manual's block table. data() exposes
//!        the raw block.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <Robotiq/gripper/register_map.hpp>
#include <Robotiq/gripper/fault_status.hpp>

namespace Robotiq {

//! gSTA — activation sequence state.
enum class ActivationState : uint8_t
{
   Reset = register_map::kActivationStateReset,
   InProgress = register_map::kActivationStateInProgress,
   Complete = register_map::kActivationStateComplete,
};

//! gOBJ — object detection state.
enum class ObjectDetection : uint8_t
{
   Moving = register_map::kObjectMoving,
   DetectedWhileOpening = register_map::kObjectDetectedOpening,
   DetectedWhileClosing = register_map::kObjectDetectedClosing,
   AtRequestedPosition = register_map::kObjectAtRequestedPosition,
};

//! The GRIPPER STATUS byte (byte 0 of the status block).
class GripperStatusFlags
{
public:
   [[nodiscard]] bool activated() const // gACT (echo of rACT)
   {
      return (_bits & register_map::kActivationStatusMask) != 0;
   }
   [[nodiscard]] bool goToEnabled() const // gGTO (echo of rGTO)
   {
      return (_bits & register_map::kGoToEchoMask) != 0;
   }
   [[nodiscard]] ActivationState activationState() const // gSTA
   {
      return static_cast<ActivationState>((_bits & register_map::kActivationStateMask)
                                          >> register_map::kActivationStateShift);
   }
   [[nodiscard]] ObjectDetection objectDetection() const // gOBJ
   {
      return static_cast<ObjectDetection>((_bits & register_map::kObjectDetectionMask)
                                          >> register_map::kObjectDetectionShift);
   }

   [[nodiscard]] uint8_t raw() const { return _bits; }

   [[nodiscard]] bool operator==(GripperStatusFlags other) const { return _bits == other._bits; }
   [[nodiscard]] bool operator!=(GripperStatusFlags other) const { return _bits != other._bits; }

private:
   uint8_t _bits = 0;
};

static_assert(std::is_standard_layout_v<GripperStatusFlags> && std::is_trivially_copyable_v<GripperStatusFlags>
                 && sizeof(GripperStatusFlags) == 1,
              "GripperStatusFlags must be a single byte");

struct GripperStatus
{
   GripperStatusFlags gripperStatus; // byte 0 — GRIPPER STATUS
   uint8_t reserved1 = 0; // byte 1
   FaultStatus faultStatus; // byte 2 — FAULT STATUS (gFLT / kFLT)
   uint8_t positionRequestEcho = 0; // byte 3 — gPR
   uint8_t position = 0; // byte 4 — gPO: 0 open .. 255 closed
   uint8_t current = 0; // byte 5 — gCU (effort proxy)

   std::array<uint8_t, register_map::kStatusBlockBytes - register_map::kDocumentedBytes> reserved{}; // bytes 6..15

   [[nodiscard]] const uint8_t* data() const { return reinterpret_cast<const uint8_t*>(this); }
   [[nodiscard]] uint8_t* data() { return reinterpret_cast<uint8_t*>(this); }
   [[nodiscard]] static constexpr std::size_t size() { return register_map::kStatusBlockBytes; }
};

[[nodiscard]] inline bool operator==(const GripperStatus& lhs, const GripperStatus& rhs)
{
   return std::memcmp(lhs.data(), rhs.data(), GripperStatus::size()) == 0;
}
[[nodiscard]] inline bool operator!=(const GripperStatus& lhs, const GripperStatus& rhs)
{
   return !(lhs == rhs);
}

static_assert(std::is_standard_layout_v<GripperStatus> && std::is_trivially_copyable_v<GripperStatus>
                 && sizeof(GripperStatus) == register_map::kStatusBlockBytes,
              "GripperStatus must overlay the raw status block exactly");
} // namespace Robotiq
