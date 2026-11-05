// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <array>

#include <Robotiq/gripper/status.hpp>
#include <Robotiq/detail/byte_packing.hpp>
#include <Robotiq/detail/modbus_constants.hpp>

namespace Robotiq::test {

// The wire contract the typed views rely on: block byte 2N is the HIGH
// byte of register N. If the packing ever went LSB-first, every field
// would silently corrupt — these tests pin the mapping.
TEST(TestBytePacking, registers_are_msb_first_and_round_trip)
{
   const std::array<uint8_t, 6> bytes = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
   const auto registers = detail::registersFromBytes<3>(bytes.data());
   EXPECT_EQ(registers[0], 0x1234);
   EXPECT_EQ(registers[1], 0x5678);
   EXPECT_EQ(registers[2], 0x9ABC);

   std::array<uint8_t, 6> back{};
   detail::bytesFromRegisters(registers, back.data());
   EXPECT_EQ(back, bytes);
}

TEST(TestBytePacking, status_fields_map_onto_the_documented_registers)
{
   GripperStatus status;
   status.positionRequestEcho = 0x42; // byte 3: low byte of register 0x07D1
   status.position = 0xE6; // byte 4: high byte of register 0x07D2
   status.current = 0x14; // byte 5: low byte of register 0x07D2

   const auto registers = detail::registersFromBytes<detail::modbus_constants::kStatusRegisterCount>(status.data());
   EXPECT_EQ(registers[1], 0x0042); // gFLT | gPR
   EXPECT_EQ(registers[2], 0xE614); // gPO | gCU
}
} // namespace Robotiq::test
