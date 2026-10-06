// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// gripper.hpp comes first and alone: the polling waits below must reach
// this TU through it, not through a header included later.
#include <Robotiq/gripper.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <type_traits>

#include <Robotiq/detail/gripper_modbus_client.hpp>
#include <Robotiq/gripper/named_bit_array.hpp>
#include <Robotiq/gripper/register_map.hpp>
#include <Robotiq/gripper/throttle.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace Robotiq::test {

TEST(TestV1_0Compat, gripper_header_brings_the_polling_waits)
{
   EXPECT_TRUE(waitFor([] { return true; }, std::chrono::milliseconds(0)));
}

TEST(TestV1_0Compat, named_bit_array_keeps_its_namespace)
{
   static_assert(std::is_same_v<NamedBitArray<ActionRequestBit>, ActionRequest>);
}

TEST(TestV1_0Compat, throttle_keeps_its_namespace)
{
   static_assert(std::is_same_v<Throttle, detail::Throttle>);
}

TEST(TestV1_0Compat, make_default_logger_keeps_its_namespace)
{
   EXPECT_EQ(makeDefaultLogger(), detail::makeDefaultLogger());
}

TEST(TestV1_0Compat, register_map_keeps_its_namespace)
{
   EXPECT_EQ(register_map::kCommandBlockBytes, detail::register_map::kCommandBlockBytes);
}

TEST(TestV1_0Compat, modbus_client_keeps_its_detail_name)
{
   static_assert(std::is_same_v<detail::GripperModbusClient, GripperModbusClient>);
}

} // namespace Robotiq::test

#pragma GCC diagnostic pop
