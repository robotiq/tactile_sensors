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
using namespace Robotiq::units;
} // namespace

TEST(TestDeviceProfile, the_2f85_carries_the_manual_ranges_and_the_measured_register_band)
{
   EXPECT_DOUBLE_EQ(k2F85.minSpeed, 0.020);
   EXPECT_DOUBLE_EQ(k2F85.fullScaleSpeed, 0.150);
   EXPECT_DOUBLE_EQ(k2F85.minForce, 20.0);
   EXPECT_DOUBLE_EQ(k2F85.fullScaleForce, 235.0);
   EXPECT_DOUBLE_EQ(k2F85.stroke, 0.085);
   EXPECT_EQ(k2F85.openRegister, 3);
   EXPECT_EQ(k2F85.closedRegister, 230);
   EXPECT_DOUBLE_EQ(k2F85.registerBand(), 227.0);
}

TEST(TestRegisterFromSpan, maps_the_span_onto_the_whole_byte)
{
   EXPECT_EQ(registerFromSpan(20.0, 20.0, 235.0), 0);
   EXPECT_EQ(registerFromSpan(127.5, 20.0, 235.0), 128); // 127.5 steps
   EXPECT_EQ(registerFromSpan(235.0, 20.0, 235.0), 255);
}

TEST(TestRegisterFromSpan, floors_below_the_minimum_and_saturates_above_full_scale)
{
   EXPECT_EQ(registerFromSpan(0.0, 20.0, 235.0), 0);
   EXPECT_EQ(registerFromSpan(10.0, 20.0, 235.0), 0);
   EXPECT_EQ(registerFromSpan(300.0, 20.0, 235.0), 255);
}

TEST(TestRegisterFromSpan, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(registerFromSpan(-1.0, 20.0, 235.0).has_value());
   EXPECT_FALSE(registerFromSpan(kNaN, 20.0, 235.0).has_value());
   EXPECT_FALSE(registerFromSpan(kInf, 20.0, 235.0).has_value());
   EXPECT_FALSE(registerFromSpan(50.0, 20.0, 20.0).has_value());
   EXPECT_FALSE(registerFromSpan(50.0, 235.0, 20.0).has_value());
   EXPECT_FALSE(registerFromSpan(50.0, kNaN, 235.0).has_value());
   EXPECT_FALSE(registerFromSpan(50.0, 20.0, kInf).has_value());
}

TEST(TestSpeedToRegister, the_manual_range_spans_the_whole_byte)
{
   EXPECT_EQ(speedToRegister(0.020, k2F85), 0);
   EXPECT_EQ(speedToRegister(0.085, k2F85), 128); // 127.5 steps
   EXPECT_EQ(speedToRegister(0.150, k2F85), 255);
}

TEST(TestSpeedToRegister, floors_and_saturates_outside_the_range)
{
   EXPECT_EQ(speedToRegister(0.0, k2F85), 0);
   EXPECT_EQ(speedToRegister(0.3, k2F85), 255);
}

TEST(TestSpeedToRegister, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(speedToRegister(-0.1, k2F85).has_value());
   EXPECT_FALSE(speedToRegister(kNaN, k2F85).has_value());

   DeviceProfile broken = k2F85;
   broken.fullScaleSpeed = broken.minSpeed;
   EXPECT_FALSE(speedToRegister(0.1, broken).has_value());
}

TEST(TestForceToRegister, the_manual_range_spans_the_whole_byte)
{
   EXPECT_EQ(forceToRegister(20.0, k2F85), 0);
   EXPECT_EQ(forceToRegister(80.0, k2F85), 71); // 71.16 steps
   EXPECT_EQ(forceToRegister(235.0, k2F85), 255);
}

TEST(TestForceToRegister, floors_saturates_and_rejects_like_speed)
{
   EXPECT_EQ(forceToRegister(10.0, k2F85), 0);
   EXPECT_EQ(forceToRegister(300.0, k2F85), 255);
   EXPECT_FALSE(forceToRegister(-1.0, k2F85).has_value());
}

TEST(TestOpeningToRegister, the_stroke_ends_land_on_the_register_band)
{
   EXPECT_EQ(openingToRegister(0.085, k2F85), 3); // fully open
   EXPECT_EQ(openingToRegister(0.0, k2F85), 230); // fully closed
}

TEST(TestOpeningToRegister, rounds_to_the_nearest_value)
{
   EXPECT_EQ(openingToRegister(0.020, k2F85), 177); // 176.59 steps
}

TEST(TestOpeningToRegister, clamps_controller_overshoot_to_the_stroke_ends)
{
   EXPECT_EQ(openingToRegister(0.085 + 1e-4, k2F85), 3);
   EXPECT_EQ(openingToRegister(-1e-4, k2F85), 230);
   EXPECT_EQ(openingToRegister(0.100, k2F85), 3);
}

TEST(TestOpeningToRegister, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(openingToRegister(kNaN, k2F85).has_value());
   EXPECT_FALSE(openingToRegister(kInf, k2F85).has_value());

   DeviceProfile noStroke = k2F85;
   noStroke.stroke = 0.0;
   EXPECT_FALSE(openingToRegister(0.02, noStroke).has_value());

   DeviceProfile noBand = k2F85;
   noBand.closedRegister = noBand.openRegister;
   EXPECT_FALSE(openingToRegister(0.02, noBand).has_value());
}

TEST(TestOpeningFromRegister, the_register_band_ends_land_on_the_stroke)
{
   EXPECT_DOUBLE_EQ(openingFromRegister(3, k2F85).value(), 0.085);
   EXPECT_DOUBLE_EQ(openingFromRegister(230, k2F85).value(), 0.0);
   EXPECT_NEAR(openingFromRegister(177, k2F85).value(), 0.019846, 1e-6);
}

TEST(TestOpeningFromRegister, values_outside_the_band_read_as_the_stroke_ends)
{
   EXPECT_DOUBLE_EQ(openingFromRegister(0, k2F85).value(), 0.085);
   EXPECT_DOUBLE_EQ(openingFromRegister(255, k2F85).value(), 0.0);
}

TEST(TestOpeningFromRegister, a_closed_gripper_settling_short_of_closed_register_reads_slightly_open)
{
   EXPECT_NEAR(openingFromRegister(228, k2F85).value(), 0.00075, 1e-5);
}

TEST(TestOpeningFromRegister, rejects_a_profile_with_no_register_band)
{
   DeviceProfile broken = k2F85;
   broken.closedRegister = broken.openRegister;
   EXPECT_FALSE(openingFromRegister(broken.openRegister, broken).has_value());
   EXPECT_FALSE(openingFromRegister(255, broken).has_value());
}

TEST(TestOpeningFromRegister, inside_the_band_a_position_read_back_commands_the_same_value)
{
   for(int count = k2F85.openRegister; count <= k2F85.closedRegister; ++count)
   {
      const auto raw = static_cast<uint8_t>(count);
      EXPECT_EQ(openingToRegister(openingFromRegister(raw, k2F85).value(), k2F85), raw) << "count " << count;
   }
}

TEST(TestMotorCurrentFromRegister, ten_milliamperes_per_step)
{
   EXPECT_DOUBLE_EQ(motorCurrentFromRegister(0), 0.0);
   EXPECT_DOUBLE_EQ(motorCurrentFromRegister(20), 0.2);
}
} // namespace Robotiq::test
