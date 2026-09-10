// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <cstddef>
#include <cstdint>

// Byte and bit layout of the Robotiq 2F adaptive grippers'
// (2F-85 / 2F-140 / Hand-E class) command and status blocks, as
// published in the gripper's instruction manual.
//
// | Byte | Robot output (command) | Robot input (status) |
// |------|------------------------|----------------------|
// | 0    | ACTION REQUEST         | GRIPPER STATUS       |
// | 1    | RESERVED               | RESERVED             |
// | 2    | RESERVED               | FAULT STATUS         |
// | 3    | POSITION REQUEST       | POS REQUEST ECHO     |
// | 4    | SPEED                  | POSITION             |
// | 5    | FORCE                  | CURRENT              |
// | 6-15 | RESERVED               | RESERVED             |
//
// Two of those bytes are themselves packed with several fields — the
// GRIPPER STATUS byte and the FAULT STATUS byte
//
// Most consumers never need any of this directly: GripperCommand and
// GripperStatus already expose the same information in a safer, easier-to-use
// format.
namespace Robotiq::register_map {

// Block layout: how wide each block is, and how many of its leading
// bytes the manual tables — the rest is reserved (see the byte table
// above).
inline constexpr std::size_t kCommandBlockBytes = 16; // total width of the command block, in bytes
inline constexpr std::size_t kStatusBlockBytes = 16; // total width of the status block, in bytes
inline constexpr std::size_t kCommandDocumentedBytes = 6; // leading command-block bytes with a tabled field
inline constexpr std::size_t kStatusDocumentedBytes = 6; // leading status-block bytes with a tabled field

// GRIPPER STATUS byte
inline constexpr uint8_t kActivationStatusMask = 0x01; // gACT
inline constexpr uint8_t kGoToEchoMask = 0x08; // gGTO
inline constexpr uint8_t kActivationStateMask = 0x30; // gSTA (bits 4-5)
inline constexpr uint8_t kObjectDetectionMask = 0xC0; // gOBJ (bits 6-7)
inline constexpr int kActivationStateShift = 4; // shift applied to gSTA after masking
inline constexpr int kObjectDetectionShift = 6; // shift applied to gOBJ after masking

// gSTA field values
inline constexpr uint8_t kActivationStateReset = 0x00; // see ActivationState::Reset
inline constexpr uint8_t kActivationStateInProgress = 0x01; // see ActivationState::InProgress
inline constexpr uint8_t kActivationStateReserved = 0x02; // see ActivationState::Reserved
inline constexpr uint8_t kActivationStateComplete = 0x03; // see ActivationState::Complete

// gOBJ field values
inline constexpr uint8_t kObjectMoving = 0x00; // see ObjectDetection::Moving
inline constexpr uint8_t kObjectDetectedOpening = 0x01; // see ObjectDetection::DetectedWhileOpening
inline constexpr uint8_t kObjectDetectedClosing = 0x02; // see ObjectDetection::DetectedWhileClosing
inline constexpr uint8_t kObjectAtRequestedPosition = 0x03; // see ObjectDetection::AtRequestedPosition

// FAULT STATUS byte
inline constexpr uint8_t kGripperFaultMask = 0x0F; // gFLT (low nibble)
inline constexpr uint8_t kControllerFaultMask = 0xF0; // kFLT (high nibble)
inline constexpr int kControllerFaultShift = 4; // shift applied to kFLT after masking

} // namespace Robotiq::register_map
