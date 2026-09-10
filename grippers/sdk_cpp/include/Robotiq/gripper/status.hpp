// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <Robotiq/gripper/register_map.hpp>
#include <Robotiq/gripper/fault_status.hpp>

namespace Robotiq {

//! \ingroup gripper_status
//! gSTA — activation sequence state.
enum class ActivationState : uint8_t
{
   Reset = register_map::kActivationStateReset, //!< not activated; rACT is clear
   InProgress = register_map::kActivationStateInProgress, //!< activation handshake running
   Reserved = register_map::kActivationStateReserved, //!< unallocated pattern; not activated
   Complete = register_map::kActivationStateComplete, //!< activated; ready for motion commands
};

//! \ingroup gripper_status
//! gOBJ — object detection state.
enum class ObjectDetection : uint8_t
{
   Moving = register_map::kObjectMoving, //!< fingers in motion, or motion not yet requested
   DetectedWhileOpening = register_map::kObjectDetectedOpening, //!< stopped early while opening
   DetectedWhileClosing = register_map::kObjectDetectedClosing, //!< stopped early while closing
   AtRequestedPosition = register_map::kObjectAtRequestedPosition, //!< reached rPR with no object stop
};

//! \ingroup gripper_status
//! \brief The GRIPPER STATUS byte (byte 0 of the status block).
//!
//! This byte is decoded through named accessors: activated() and goToEnabled() answer single
//! flag bits, while activationState() and objectDetection() unpack
//! multi-bit fields into their own enums (ActivationState, ObjectDetection).
//!
//! \par Example
//! \snippet snippets.cpp gripper-status-flags
class GripperStatusFlags
{
public:
   //! \brief Synthesize a byte the gripper would have sent.
   //!
   //! For simulators and for exercising status handling without hardware.
   //! \param bits The raw GRIPPER STATUS byte.
   //! \return A GripperStatusFlags wrapping \p bits.
   [[nodiscard]] static constexpr GripperStatusFlags fromRaw(uint8_t bits)
   {
      GripperStatusFlags flags;
      flags._bits = bits;
      return flags;
   }

   //! \return gACT — the echo of the last-sent rACT (activation request).
   [[nodiscard]] bool activated() const { return (_bits & register_map::kActivationStatusMask) != 0; }

   //! \return gGTO — the echo of the last-sent rGTO (go-to request).
   [[nodiscard]] bool goToEnabled() const { return (_bits & register_map::kGoToEchoMask) != 0; }

   //! \return gSTA — where the activation handshake stands; see ActivationState.
   [[nodiscard]] ActivationState activationState() const
   {
      return static_cast<ActivationState>((_bits & register_map::kActivationStateMask)
                                          >> register_map::kActivationStateShift);
   }

   //! \return gOBJ — whether the fingers are moving or stopped on an object; see ObjectDetection.
   [[nodiscard]] ObjectDetection objectDetection() const
   {
      return static_cast<ObjectDetection>((_bits & register_map::kObjectDetectionMask)
                                          >> register_map::kObjectDetectionShift);
   }

   //! \return The raw GRIPPER STATUS byte.
   [[nodiscard]] uint8_t raw() const { return _bits; }

   //! \return true if both flag bytes are identical.
   [[nodiscard]] bool operator==(GripperStatusFlags other) const { return _bits == other._bits; }
   //! \return true if the flag bytes differ.
   [[nodiscard]] bool operator!=(GripperStatusFlags other) const { return _bits != other._bits; }

private:
   uint8_t _bits = 0;
};

static_assert(std::is_standard_layout_v<GripperStatusFlags> && std::is_trivially_copyable_v<GripperStatusFlags>
                 && sizeof(GripperStatusFlags) == 1,
              "GripperStatusFlags must be a single byte");

//! \ingroup status
//! \brief The gripper status block (gripper -> host), laid out byte for
//! byte as the instruction manual's block table.
//!
//! \par Example
//! \snippet snippets.cpp motion-settled-example
struct GripperStatus
{
   GripperStatusFlags gripperStatus; //!< byte 0 — GRIPPER STATUS
   uint8_t reserved1 = 0; //!< byte 1
   FaultStatus faultStatus; //!< byte 2 — FAULT STATUS (gFLT / kFLT)
   uint8_t positionRequestEcho = 0; //!< byte 3 — gPR
   uint8_t position = 0; //!< byte 4 — gPO: 0 open .. 255 closed
   uint8_t current = 0; //!< byte 5 — gCU (effort proxy)

   //! bytes 6..15
   std::array<uint8_t, register_map::kStatusBlockBytes - register_map::kStatusDocumentedBytes> reservedTail{};

   //! \return Raw block access, the manual's byte order. size() bytes wide.
   [[nodiscard]] const uint8_t* data() const { return reinterpret_cast<const uint8_t*>(this); }
   //! \overload
   [[nodiscard]] uint8_t* data() { return reinterpret_cast<uint8_t*>(this); }
   //! \return The width of the status block in bytes (16, per the manual).
   [[nodiscard]] static constexpr std::size_t size() { return register_map::kStatusBlockBytes; }
};

//! \return true if every byte of the two status blocks is identical.
[[nodiscard]] inline bool operator==(const GripperStatus& lhs, const GripperStatus& rhs)
{
   return std::memcmp(lhs.data(), rhs.data(), GripperStatus::size()) == 0;
}
//! \return true if any byte of the two status blocks differs.
[[nodiscard]] inline bool operator!=(const GripperStatus& lhs, const GripperStatus& rhs)
{
   return !(lhs == rhs);
}

static_assert(std::is_standard_layout_v<GripperStatus> && std::is_trivially_copyable_v<GripperStatus>
                 && sizeof(GripperStatus) == register_map::kStatusBlockBytes,
              "GripperStatus must overlay the raw status block exactly");
} // namespace Robotiq
