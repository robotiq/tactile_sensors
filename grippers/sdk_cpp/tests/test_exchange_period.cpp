// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <limits>

#include <Robotiq/gripper/driver_exception.hpp>

#include "exchange_period.hpp"

namespace Robotiq::detail::test {

TEST(ExchangePeriodFromFrequency, ConvertsHertzToAPeriod)
{
   EXPECT_EQ(std::chrono::microseconds{10000}, exchangePeriodFromFrequency(100.0));
   EXPECT_EQ(std::chrono::microseconds{1000}, exchangePeriodFromFrequency(1000.0));
   EXPECT_EQ(std::chrono::microseconds{2000000}, exchangePeriodFromFrequency(0.5));
}

TEST(ExchangePeriodFromFrequency, FreeRunIsAZeroPeriod)
{
   // A zero period is what the exchange loop paces past immediately, which on
   // a real link means "as fast as the bus allows".
   EXPECT_EQ(std::chrono::microseconds{0}, exchangePeriodFromFrequency(0.0));
}

TEST(ExchangePeriodFromFrequency, PassesOnARateItCannotUse)
{
   EXPECT_THROW(exchangePeriodFromFrequency(-1.0), DriverException);
}
} // namespace Robotiq::detail::test
