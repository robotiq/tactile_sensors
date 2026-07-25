// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "fake/status_writer.hpp"

#include <cstddef>

#include <Robotiq/gripper/register_map.hpp>

namespace Robotiq::fake {
namespace {
namespace rm = register_map;

// Byte offsets of the two packed bytes, from the manual's block table. The
// only place in the library that needs them: everything else reaches the
// status through named members or the readers on the shipped type.
constexpr std::size_t kStatusFlagsByte = 0;
constexpr std::size_t kFaultByte = 2;

//! Replace the bits under \p mask with \p value, already shifted into place.
uint8_t withField(uint8_t bits, uint8_t mask, uint8_t value)
{
   return static_cast<uint8_t>((bits & static_cast<uint8_t>(~mask)) | (value & mask));
}

uint8_t withFlag(uint8_t bits, uint8_t mask, bool set)
{
   return static_cast<uint8_t>(set ? (bits | mask) : (bits & static_cast<uint8_t>(~mask)));
}
} // namespace

void setActivated(GripperStatus& status, bool activated)
{
   uint8_t& bits = status.data()[kStatusFlagsByte];
   bits = withFlag(bits, rm::kActivationStatusMask, activated);
}

void setGoToEnabled(GripperStatus& status, bool enabled)
{
   uint8_t& bits = status.data()[kStatusFlagsByte];
   bits = withFlag(bits, rm::kGoToEchoMask, enabled);
}

void setActivationState(GripperStatus& status, ActivationState state)
{
   uint8_t& bits = status.data()[kStatusFlagsByte];
   bits = withField(bits,
                    rm::kActivationStateMask,
                    static_cast<uint8_t>(static_cast<uint8_t>(state) << rm::kActivationStateShift));
}

void setObjectDetection(GripperStatus& status, ObjectDetection detection)
{
   uint8_t& bits = status.data()[kStatusFlagsByte];
   bits = withField(bits,
                    rm::kObjectDetectionMask,
                    static_cast<uint8_t>(static_cast<uint8_t>(detection) << rm::kObjectDetectionShift));
}

void setGripperFault(GripperStatus& status, GripperFault fault)
{
   uint8_t& bits = status.data()[kFaultByte];
   bits = withField(bits, rm::kGripperFaultMask, static_cast<uint8_t>(fault));
}

void setStatusFlagsByte(GripperStatus& status, uint8_t byte)
{
   status.data()[kStatusFlagsByte] = byte;
}

void setFaultStatusByte(GripperStatus& status, uint8_t byte)
{
   status.data()[kFaultByte] = byte;
}
} // namespace Robotiq::fake
