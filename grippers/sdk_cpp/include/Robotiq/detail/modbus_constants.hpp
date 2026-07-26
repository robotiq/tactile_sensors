// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Modbus wire framing for the Robotiq 2F command and status
//!        blocks. The byte-level meaning of each block is in
//!        Robotiq/gripper/register_map.hpp. As transport framing it is
//!        exempt from the API stability promise.

#pragma once

#include <cstdint>

#include <Robotiq/gripper/register_map.hpp>

namespace Robotiq::detail::modbus_constants {

// Only the documented bytes travel on this bandwidth-precious wire,
// packed in register pairs — each block counted on its own, as
// register_map does.

// Command block (host → gripper). Written with FC 0x10 (or FC 0x17).
inline constexpr uint16_t kCommandAddress = 0x03E8; // 1000 decimal
inline constexpr uint16_t kCommandRegisterCount = register_map::kCommandDocumentedBytes / 2;

// Status block (gripper → host). Read with FC 0x03 (or FC 0x17).
inline constexpr uint16_t kStatusAddress = 0x07D0; // 2000 decimal
inline constexpr uint16_t kStatusRegisterCount = register_map::kStatusDocumentedBytes / 2;

// Registers are 16-bit, so consecutive block bytes pack in pairs, MSB
// first: register N carries block byte 2N in its high byte and byte 2N+1
// in its low byte. The six documented bytes of each block thus travel as
// the three registers above.

} // namespace Robotiq::detail::modbus_constants
