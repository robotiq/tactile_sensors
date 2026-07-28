// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The gripper command block (host -> gripper), laid out byte for
//!        byte as the instruction manual's block table. Single-byte
//!        fields are plain members; the packed action byte (byte 0) is a
//!        NamedBitArray of the manual's action bits. data() exposes the
//!        raw block for the transport and for no-thread integrations.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <Robotiq/gripper/register_map.hpp>
#include <Robotiq/gripper/named_bit_array.hpp>

namespace Robotiq {

// Bit layout of the action-request byte (byte 0 of the command block):
//   bit    7     6     5     4     3     2     1     0
//         rsvd  rsvd  rARD  rATR  rGTO  rsvd  rsvd  rACT
enum class ActionRequestBit : uint8_t
{
   Activate = 0, // rACT — must stay set after activation
   GoTo = 3, // rGTO — go to the requested position
   AutoRelease = 4, // rATR — automatic (emergency) release
   AutoReleaseOpenDirection = 5, // rARD — set = open direction
};

using ActionRequest = NamedBitArray<ActionRequestBit>;

struct GripperCommand
{
   ActionRequest action{}; // byte 0 — ACTION REQUEST
   uint8_t reserved1 = 0; // byte 1
   uint8_t reserved2 = 0; // byte 2
   uint8_t positionRequest = 0; // byte 3 — rPR: 0 open .. 255 closed
   uint8_t speed = 0; // byte 4 — rSP
   uint8_t force = 0; // byte 5 — rFR

   std::array<uint8_t, register_map::kCommandBlockBytes - register_map::kCommandDocumentedBytes>
      reservedTail{}; // bytes 6..15

   // A ready-to-use command: activated (rACT), no motion, and the speed
   // and force the Robotiq URCap defaults to (both maximum). Default
   // construction is all-zero; opt into these values.
   [[nodiscard]] constexpr static GripperCommand defaults()
   {
      GripperCommand command;
      command.action.set(ActionRequestBit::Activate);
      command.speed = 0xFF;
      command.force = 0xFF;
      return command;
   }

   // Raw block access, the manual's byte order. size() bytes wide.
   [[nodiscard]] const uint8_t* data() const { return reinterpret_cast<const uint8_t*>(this); }
   [[nodiscard]] uint8_t* data() { return reinterpret_cast<uint8_t*>(this); }
   [[nodiscard]] static constexpr std::size_t size() { return register_map::kCommandBlockBytes; }
};

[[nodiscard]] inline bool operator==(const GripperCommand& lhs, const GripperCommand& rhs)
{
   return std::memcmp(lhs.data(), rhs.data(), GripperCommand::size()) == 0;
}
[[nodiscard]] inline bool operator!=(const GripperCommand& lhs, const GripperCommand& rhs)
{
   return !(lhs == rhs);
}

static_assert(std::is_standard_layout_v<GripperCommand> && std::is_trivially_copyable_v<GripperCommand>
                 && sizeof(GripperCommand) == register_map::kCommandBlockBytes,
              "GripperCommand must overlay the raw command block exactly");
} // namespace Robotiq
