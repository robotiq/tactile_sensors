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

TEST(TestDeviceProfile, the_2f85_carries_the_manual_ranges_and_the_measured_count_band)
{
   EXPECT_DOUBLE_EQ(k2F85.minSpeed, 0.020);
   EXPECT_DOUBLE_EQ(k2F85.fullScaleSpeed, 0.150);
   EXPECT_DOUBLE_EQ(k2F85.minForce, 20.0);
   EXPECT_DOUBLE_EQ(k2F85.fullScaleForce, 235.0);
   EXPECT_DOUBLE_EQ(k2F85.stroke, 0.085);
   EXPECT_EQ(k2F85.openCount, 3);
   EXPECT_EQ(k2F85.closedCount, 230);
   EXPECT_DOUBLE_EQ(k2F85.countBand(), 227.0);
}

TEST(TestCountFromSpan, maps_the_span_onto_the_whole_byte)
{
   EXPECT_EQ(countFromSpan(20.0, 20.0, 235.0), 0);
   EXPECT_EQ(countFromSpan(127.5, 20.0, 235.0), 128); // 127.5 counts
   EXPECT_EQ(countFromSpan(235.0, 20.0, 235.0), 255);
}

TEST(TestCountFromSpan, floors_below_the_minimum_and_saturates_above_full_scale)
{
   EXPECT_EQ(countFromSpan(0.0, 20.0, 235.0), 0);
   EXPECT_EQ(countFromSpan(10.0, 20.0, 235.0), 0);
   EXPECT_EQ(countFromSpan(300.0, 20.0, 235.0), 255);
}

TEST(TestCountFromSpan, rejects_what_has_no_count)
{
   EXPECT_FALSE(countFromSpan(-1.0, 20.0, 235.0).has_value());
   EXPECT_FALSE(countFromSpan(kNaN, 20.0, 235.0).has_value());
   EXPECT_FALSE(countFromSpan(kInf, 20.0, 235.0).has_value());
   EXPECT_FALSE(countFromSpan(50.0, 20.0, 20.0).has_value());
   EXPECT_FALSE(countFromSpan(50.0, 235.0, 20.0).has_value());
   EXPECT_FALSE(countFromSpan(50.0, kNaN, 235.0).has_value());
   EXPECT_FALSE(countFromSpan(50.0, 20.0, kInf).has_value());
}

TEST(TestSpeedToCount, the_manual_range_spans_the_whole_byte)
{
   EXPECT_EQ(speedToCount(0.020, k2F85), 0);
   EXPECT_EQ(speedToCount(0.085, k2F85), 128); // 127.5 counts
   EXPECT_EQ(speedToCount(0.150, k2F85), 255);
}

TEST(TestSpeedToCount, floors_and_saturates_outside_the_range)
{
   EXPECT_EQ(speedToCount(0.0, k2F85), 0);
   EXPECT_EQ(speedToCount(0.3, k2F85), 255);
}

TEST(TestSpeedToCount, rejects_what_has_no_count)
{
   EXPECT_FALSE(speedToCount(-0.1, k2F85).has_value());
   EXPECT_FALSE(speedToCount(kNaN, k2F85).has_value());

   DeviceProfile broken = k2F85;
   broken.fullScaleSpeed = broken.minSpeed;
   EXPECT_FALSE(speedToCount(0.1, broken).has_value());
}

TEST(TestForceToCount, the_manual_range_spans_the_whole_byte)
{
   EXPECT_EQ(forceToCount(20.0, k2F85), 0);
   EXPECT_EQ(forceToCount(80.0, k2F85), 71); // 71.16 counts
   EXPECT_EQ(forceToCount(235.0, k2F85), 255);
}

TEST(TestForceToCount, floors_saturates_and_rejects_like_speed)
{
   EXPECT_EQ(forceToCount(10.0, k2F85), 0);
   EXPECT_EQ(forceToCount(300.0, k2F85), 255);
   EXPECT_FALSE(forceToCount(-1.0, k2F85).has_value());
}

TEST(TestOpeningToCount, the_stroke_ends_land_on_the_count_band)
{
   EXPECT_EQ(openingToCount(0.085, k2F85), 3); // fully open
   EXPECT_EQ(openingToCount(0.0, k2F85), 230); // fully closed
}

TEST(TestOpeningToCount, rounds_to_the_nearest_count)
{
   EXPECT_EQ(openingToCount(0.020, k2F85), 177); // 176.59 counts
}

TEST(TestOpeningToCount, clamps_controller_overshoot_to_the_stroke_ends)
{
   EXPECT_EQ(openingToCount(0.085 + 1e-4, k2F85), 3);
   EXPECT_EQ(openingToCount(-1e-4, k2F85), 230);
   EXPECT_EQ(openingToCount(0.100, k2F85), 3);
}

TEST(TestOpeningToCount, rejects_what_has_no_count)
{
   EXPECT_FALSE(openingToCount(kNaN, k2F85).has_value());
   EXPECT_FALSE(openingToCount(kInf, k2F85).has_value());

   DeviceProfile noStroke = k2F85;
   noStroke.stroke = 0.0;
   EXPECT_FALSE(openingToCount(0.02, noStroke).has_value());

   DeviceProfile noBand = k2F85;
   noBand.closedCount = noBand.openCount;
   EXPECT_FALSE(openingToCount(0.02, noBand).has_value());
}

TEST(TestOpeningFromCount, the_count_band_ends_land_on_the_stroke)
{
   EXPECT_DOUBLE_EQ(openingFromCount(3, k2F85).value(), 0.085);
   EXPECT_DOUBLE_EQ(openingFromCount(230, k2F85).value(), 0.0);
   EXPECT_NEAR(openingFromCount(177, k2F85).value(), 0.019846, 1e-6);
}

TEST(TestOpeningFromCount, counts_outside_the_band_read_as_the_stroke_ends)
{
   EXPECT_DOUBLE_EQ(openingFromCount(0, k2F85).value(), 0.085);
   EXPECT_DOUBLE_EQ(openingFromCount(255, k2F85).value(), 0.0);
}

TEST(TestOpeningFromCount, a_closed_gripper_settling_short_of_closed_count_reads_slightly_open)
{
   EXPECT_NEAR(openingFromCount(228, k2F85).value(), 0.00075, 1e-5);
}

TEST(TestOpeningFromCount, rejects_a_profile_with_no_count_band)
{
   DeviceProfile broken = k2F85;
   broken.closedCount = broken.openCount;
   EXPECT_FALSE(openingFromCount(broken.openCount, broken).has_value());
   EXPECT_FALSE(openingFromCount(255, broken).has_value());
}

TEST(TestOpeningFromCount, inside_the_band_a_position_read_back_commands_the_same_count)
{
   for(int count = k2F85.openCount; count <= k2F85.closedCount; ++count)
   {
      const auto raw = static_cast<uint8_t>(count);
      EXPECT_EQ(openingToCount(openingFromCount(raw, k2F85).value(), k2F85), raw) << "count " << count;
   }
}

TEST(TestMotorCurrentFromCount, ten_milliamperes_per_count)
{
   EXPECT_DOUBLE_EQ(motorCurrentFromCount(0), 0.0);
   EXPECT_DOUBLE_EQ(motorCurrentFromCount(20), 0.2);
}
} // namespace Robotiq::test
