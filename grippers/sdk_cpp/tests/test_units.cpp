// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include <Robotiq/gripper/device_profile.hpp>
#include <Robotiq/gripper/units.hpp>

namespace Robotiq::test {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
const DeviceProfile& k2F85 = profiles::k2F85;
} // namespace

TEST(TestDeviceProfile, the_2f85_carries_the_manual_specification)
{
   EXPECT_DOUBLE_EQ(k2F85.fullScaleSpeed, 0.150);
   EXPECT_DOUBLE_EQ(k2F85.fullScaleForce, 235.0);
   EXPECT_DOUBLE_EQ(k2F85.stroke, 0.085);
   EXPECT_EQ(k2F85.openCount, 3);
   EXPECT_EQ(k2F85.closedCount, 230);
}

TEST(TestSpeedRegister, spans_the_whole_byte)
{
   EXPECT_EQ(speedRegister(0.0, k2F85), 0);
   EXPECT_EQ(speedRegister(0.150, k2F85), 255);
}

TEST(TestSpeedRegister, rounds_to_the_nearest_count)
{
   EXPECT_EQ(speedRegister(0.075, k2F85), 128); // 127.5 counts
}

TEST(TestSpeedRegister, saturates_above_full_scale)
{
   EXPECT_EQ(speedRegister(0.3, k2F85), 255);
}

TEST(TestSpeedRegister, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(speedRegister(-0.1, k2F85).has_value());
   EXPECT_FALSE(speedRegister(kNaN, k2F85).has_value());
   EXPECT_FALSE(speedRegister(kInf, k2F85).has_value());

   DeviceProfile broken = k2F85;
   broken.fullScaleSpeed = 0.0;
   EXPECT_FALSE(speedRegister(0.1, broken).has_value());
}

TEST(TestForceRegister, spans_the_whole_byte)
{
   EXPECT_EQ(forceRegister(0.0, k2F85), 0);
   EXPECT_EQ(forceRegister(117.5, k2F85), 128);
   EXPECT_EQ(forceRegister(235.0, k2F85), 255);
}

TEST(TestForceRegister, saturates_above_full_scale_and_rejects_the_rest)
{
   EXPECT_EQ(forceRegister(300.0, k2F85), 255);
   EXPECT_FALSE(forceRegister(-1.0, k2F85).has_value());
   EXPECT_FALSE(forceRegister(kNaN, k2F85).has_value());
}

TEST(TestOpeningRegister, the_stroke_ends_land_on_the_count_band)
{
   EXPECT_EQ(openingRegister(0.085, k2F85), 3); // fully open
   EXPECT_EQ(openingRegister(0.0, k2F85), 230); // fully closed
}

TEST(TestOpeningRegister, rounds_to_the_nearest_count)
{
   EXPECT_EQ(openingRegister(0.020, k2F85), 177); // 176.59 counts
}

TEST(TestOpeningRegister, clamps_to_the_mechanical_range)
{
   EXPECT_EQ(openingRegister(0.100, k2F85), 3);
   EXPECT_EQ(openingRegister(-0.010, k2F85), 230);
}

TEST(TestOpeningRegister, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(openingRegister(kNaN, k2F85).has_value());
   EXPECT_FALSE(openingRegister(kInf, k2F85).has_value());

   DeviceProfile broken = k2F85;
   broken.stroke = 0.0;
   EXPECT_FALSE(openingRegister(0.02, broken).has_value());
}

TEST(TestOpeningFromRegister, the_count_band_ends_land_on_the_stroke)
{
   EXPECT_DOUBLE_EQ(openingFromRegister(3, k2F85).value(), 0.085);
   EXPECT_DOUBLE_EQ(openingFromRegister(230, k2F85).value(), 0.0);
   EXPECT_NEAR(openingFromRegister(177, k2F85).value(), 0.019846, 1e-6);
}

TEST(TestOpeningFromRegister, counts_outside_the_band_read_as_the_stroke_ends)
{
   EXPECT_DOUBLE_EQ(openingFromRegister(0, k2F85).value(), 0.085);
   EXPECT_DOUBLE_EQ(openingFromRegister(255, k2F85).value(), 0.0);
}

TEST(TestOpeningFromRegister, rejects_a_profile_with_no_count_band)
{
   DeviceProfile broken = k2F85;
   broken.closedCount = broken.openCount;
   EXPECT_FALSE(openingFromRegister(broken.openCount, broken).has_value());
   EXPECT_FALSE(openingFromRegister(255, broken).has_value());
}

TEST(TestOpeningFromRegister, a_position_read_back_commands_the_same_count)
{
   for(int count = k2F85.openCount; count <= k2F85.closedCount; ++count)
   {
      const auto raw = static_cast<uint8_t>(count);
      EXPECT_EQ(openingRegister(openingFromRegister(raw, k2F85).value(), k2F85), raw) << "count " << count;
   }
}

TEST(TestMotorCurrentFromRegister, ten_milliamperes_per_count)
{
   EXPECT_DOUBLE_EQ(motorCurrentFromRegister(0), 0.0);
   EXPECT_DOUBLE_EQ(motorCurrentFromRegister(20), 0.2);
}
} // namespace Robotiq::test
