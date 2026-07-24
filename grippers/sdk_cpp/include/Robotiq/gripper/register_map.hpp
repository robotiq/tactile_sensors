// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Byte layout of the Robotiq 2F adaptive grippers'
//!        (2F-85 / 2F-140 / Hand-E class) command and status blocks, as
//!        published in the gripper's instruction manual. The blocks are
//!        byte-addressed:
//!
//!   Byte     Robot output (command)     Robot input (status)
//!   0        ACTION REQUEST             GRIPPER STATUS
//!   1        RESERVED                   RESERVED
//!   2        RESERVED                   FAULT STATUS
//!   3        POSITION REQUEST           POS REQUEST ECHO
//!   4        SPEED                      POSITION
//!   5        FORCE                      CURRENT
//!   6-15     RESERVED                   RESERVED

#pragma once

#include <cstddef>
#include <cstdint>

namespace Robotiq::register_map {

inline constexpr std::size_t kCommandBlockBytes = 16;
inline constexpr std::size_t kStatusBlockBytes = 16;

// Bit layout of the gripper-status byte (byte 0 of the status block):
//   bit    7     6     5     4     3     2     1     0
//         gOBJ  gOBJ  gSTA  gSTA  gGTO  rsvd  rsvd  gACT
inline constexpr uint8_t kActivationStatusMask = 0x01; // gACT
inline constexpr uint8_t kGoToEchoMask = 0x08; // gGTO
inline constexpr uint8_t kActivationStateMask = 0x30; // gSTA (bits 4-5)
inline constexpr uint8_t kObjectDetectionMask = 0xC0; // gOBJ (bits 6-7)
inline constexpr int kActivationStateShift = 4;
inline constexpr int kObjectDetectionShift = 6;

// gSTA values (after shift).
inline constexpr uint8_t kActivationStateReset = 0x00;
inline constexpr uint8_t kActivationStateInProgress = 0x01;
inline constexpr uint8_t kActivationStateComplete = 0x03;

// gOBJ values (after shift).
inline constexpr uint8_t kObjectMoving = 0x00; // fingers in motion (if rGTO)
inline constexpr uint8_t kObjectDetectedOpening = 0x01; // stopped while opening
inline constexpr uint8_t kObjectDetectedClosing = 0x02; // stopped while closing
inline constexpr uint8_t kObjectAtRequestedPosition = 0x03;

// The FAULT STATUS byte (byte 2) splits into two nibbles: the gripper's
// own fault (gFLT) in the low nibble, the optional controller fault
// (kFLT) in the high nibble.
inline constexpr uint8_t kGripperFaultMask = 0x0F; // gFLT
inline constexpr uint8_t kControllerFaultMask = 0xF0; // kFLT
inline constexpr int kControllerFaultShift = 4;

} // namespace Robotiq::register_map
