// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <limits>

#include <Robotiq/gripper/driver_exception.hpp>

#include "frequency.hpp"

namespace Robotiq::detail::test {

TEST(ClampFrequency, LeavesTheSupportedRangeAlone)
{
   EXPECT_DOUBLE_EQ(clampFrequency(kMinFrequency), kMinFrequency);
   EXPECT_DOUBLE_EQ(clampFrequency(100.0), 100.0);
   EXPECT_DOUBLE_EQ(clampFrequency(kMaxFrequency), kMaxFrequency);
}

TEST(ClampFrequency, FoldsTheEndsIntoTheRange)
{
   EXPECT_DOUBLE_EQ(clampFrequency(0.001), kMinFrequency);
   EXPECT_DOUBLE_EQ(clampFrequency(1e9), kMaxFrequency);
   EXPECT_DOUBLE_EQ(clampFrequency(std::numeric_limits<double>::max()), kMaxFrequency);
   // An infinity is just a very high rate as far as the clamp is concerned.
   EXPECT_DOUBLE_EQ(clampFrequency(std::numeric_limits<double>::infinity()), kMaxFrequency);
}

TEST(ClampFrequency, ClampsRatherThanStepping)
{
   // The rate you get is monotonic in the rate you asked for, so asking for
   // slightly more never hands back dramatically less.
   double previous = 0.0;
   for(const double requested : {0.001, 0.05, kMinFrequency, 1.0, 100.0, kMaxFrequency, 1e6})
   {
      const double got = clampFrequency(requested);
      EXPECT_GE(got, previous) << "not monotonic at " << requested;
      previous = got;
   }
}

TEST(ClampFrequency, PassesFreeRunThrough)
{
   // 0 keeps its meaning here; what free-run costs is the transport's business.
   EXPECT_DOUBLE_EQ(clampFrequency(0.0), 0.0);
}

TEST(ClampFrequency, RejectsWhatCannotBeARate)
{
   // NaN matters most: it survives a clamp and turns into a period that leaves
   // the exchange loop spinning rather than pacing.
   EXPECT_THROW(clampFrequency(-1.0), DriverException);
   EXPECT_THROW(clampFrequency(std::numeric_limits<double>::quiet_NaN()), DriverException);
   EXPECT_THROW(clampFrequency(-std::numeric_limits<double>::infinity()), DriverException);
}
} // namespace Robotiq::detail::test
