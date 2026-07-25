// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <limits>

#include "exchange_period.hpp"
#include "fake/frequency.hpp"

namespace Robotiq::fake::test {

TEST(ClampFrequency, LeavesTheSupportedRangeAlone)
{
   EXPECT_DOUBLE_EQ(0.5, clampFrequency(0.5));
   EXPECT_DOUBLE_EQ(100.0, clampFrequency(100.0));
   EXPECT_DOUBLE_EQ(kMinFrequency, clampFrequency(kMinFrequency));
   EXPECT_DOUBLE_EQ(kMaxFrequency, clampFrequency(kMaxFrequency));
}

TEST(ClampFrequency, ClampsRatherThanStepping)
{
   // The whole point of clamping over a fallback: the rate you get is
   // monotonic in the rate you asked for, so asking for slightly more never
   // hands back dramatically less.
   EXPECT_DOUBLE_EQ(kMinFrequency, clampFrequency(0.01));
   EXPECT_DOUBLE_EQ(kMaxFrequency, clampFrequency(50000.0));

   double previous = 0.0;
   for(const double requested : {0.001, 0.05, kMinFrequency, 1.0, 100.0, kMaxFrequency, 1e6})
   {
      const double got = clampFrequency(requested);
      EXPECT_GE(got, previous) << "not monotonic at " << requested;
      previous = got;
   }
}

TEST(ClampFrequency, FreeRunBecomesTheFastestSupportedRate)
{
   // 0 means "as fast as the link allows"; with no link, that reads as the
   // ceiling. Clamping it to the floor would invert the request.
   EXPECT_DOUBLE_EQ(kMaxFrequency, clampFrequency(0.0));
   EXPECT_DOUBLE_EQ(kMaxFrequency, clampFrequency(-1.0));
}

TEST(ClampFrequency, HandlesAnyRealValue)
{
   // Any real value maps into the supported range and none of it reaches the
   // exchange loop as a zero or negative period. NaN is out of contract: it
   // is a bug in the calling code, and absorbing it here would hide it.
   EXPECT_GT(clampFrequency(std::numeric_limits<double>::infinity()), 0.0);
   EXPECT_LE(clampFrequency(std::numeric_limits<double>::infinity()), kMaxFrequency);
   EXPECT_GT(detail::exchangePeriodFromFrequency(clampFrequency(1e300)).count(), 0);
}
} // namespace Robotiq::fake::test
