// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Writers for the packed fields of a GripperStatus.
//! GripperCommand encapsulates its packed byte behind NamedBitArray, so nobody
//! shifts bits to build a command. GripperStatus deliberately has no
//! equivalent: an application receives a status, it never authors one, so the
//! shipped type exposes readers only.
//!
//! A fake device does author one. These are the writers it needs, gathered
//! here so the block's bit layout is encoded once rather than smeared across
//! the device model and the tests that stage it.

#pragma once

#include <cstdint>

#include <Robotiq/gripper/fault_status.hpp>
#include <Robotiq/gripper/status.hpp>

namespace Robotiq::fake {

void setActivated(GripperStatus& status, bool activated);

void setGoToEnabled(GripperStatus& status, bool enabled);

void setActivationState(GripperStatus& status, ActivationState state);

void setObjectDetection(GripperStatus& status, ObjectDetection detection);

void setGripperFault(GripperStatus& status, GripperFault fault);

void setStatusFlagsByte(GripperStatus& status, uint8_t byte);
void setFaultStatusByte(GripperStatus& status, uint8_t byte);
} // namespace Robotiq::fake
