// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/throttle.hpp>

namespace Robotiq::test {
namespace {
//! Captures every delivered line, for asserting on logf formatting.
class CollectingLogger : public Logger
{
public:
   void log(Level level, std::string_view message) override { lines.emplace_back(level, std::string(message)); }

   std::vector<std::pair<Level, std::string>> lines;
};
} // namespace

TEST(TestLogger, logf_formats_printf_style)
{
   CollectingLogger logger;

   logger.logf(Logger::Level::Info, "position %d of %s", 42, "255");

   ASSERT_EQ(logger.lines.size(), 1U);
   EXPECT_EQ(logger.lines[0].first, Logger::Level::Info);
   EXPECT_EQ(logger.lines[0].second, "position 42 of 255");
}

TEST(TestLogger, makeDefaultLogger_returns_a_logger)
{
   EXPECT_NE(makeDefaultLogger(), nullptr);
}

TEST(TestStderrLogger, log_writes_the_message_to_stderr)
{
   StderrLogger logger;

   testing::internal::CaptureStderr();
   logger.log(Logger::Level::Error, "boom");
   const std::string out = testing::internal::GetCapturedStderr();

   EXPECT_NE(out.find("boom"), std::string::npos);
}

TEST(TestThrottle, executes_at_most_once_per_period)
{
   Throttle throttle(std::chrono::milliseconds(200));
   const auto t0 = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
   int calls = 0;
   const auto count = [&] { ++calls; };

   throttle.executeIfAllowed(t0, count);
   throttle.executeIfAllowed(t0, count);
   throttle.executeIfAllowed(t0 + std::chrono::milliseconds(199), count);
   EXPECT_EQ(calls, 1);
   throttle.executeIfAllowed(t0 + std::chrono::milliseconds(200), count);
   EXPECT_EQ(calls, 2);
   throttle.executeIfAllowed(t0 + std::chrono::milliseconds(399), count);
   EXPECT_EQ(calls, 2);
   throttle.executeIfAllowed(t0 + std::chrono::milliseconds(400), count);
   EXPECT_EQ(calls, 3);
}

TEST(TestThrottle, instances_throttle_independently)
{
   Throttle a(std::chrono::milliseconds(200));
   Throttle b(std::chrono::milliseconds(200));
   const auto t0 = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
   int calls = 0;

   a.executeIfAllowed(t0, [&] { ++calls; });
   b.executeIfAllowed(t0, [&] { ++calls; });
   EXPECT_EQ(calls, 2);
}

TEST(TestNullLogger, swallows_everything)
{
   NullLogger logger;

   testing::internal::CaptureStderr();
   logger.log(Logger::Level::Error, "x");
   logger.logf(Logger::Level::Error, "%d", 1);

   EXPECT_TRUE(testing::internal::GetCapturedStderr().empty());
}
} // namespace Robotiq::test
