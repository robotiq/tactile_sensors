// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief MSB-first packing between a gripper byte block and the 16-bit
//!        registers the Modbus wire carries: byte 2N is the high byte of
//!        register N. Touches only the defined registers at the start;
//!        the block's reserved tail is left alone. Used only at the
//!        nanoMODBUS boundary — the one place register framing is real.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Robotiq::detail {

// Before "optimizing" the shift loops below into a memcpy or bit_cast:
// don't. The shifts build each register arithmetically, so they encode
// the wire's MSB-first order independently of host endianness. A memcpy
// would reinterpret the bytes in host order — byte-swapped on our
// little-endian targets, and silently wrong on a big-endian host. There
// is also nothing to gain: RegisterCount is a compile-time constant, so
// these loops fully unroll.

template <std::size_t RegisterCount>
[[nodiscard]] std::array<uint16_t, RegisterCount> registersFromBytes(const uint8_t* block)
{
   std::array<uint16_t, RegisterCount> registers{};
   for(std::size_t i = 0; i < RegisterCount; ++i)
   {
      registers[i] = static_cast<uint16_t>((block[2 * i] << 8) | block[2 * i + 1]);
   }
   return registers;
}

template <std::size_t RegisterCount>
void bytesFromRegisters(const std::array<uint16_t, RegisterCount>& registers, uint8_t* block)
{
   for(std::size_t i = 0; i < RegisterCount; ++i)
   {
      block[2 * i] = static_cast<uint8_t>(registers[i] >> 8);
      block[2 * i + 1] = static_cast<uint8_t>(registers[i] & 0xFF);
   }
}
} // namespace Robotiq::detail
