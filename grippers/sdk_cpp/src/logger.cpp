// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/gripper/logger.hpp>

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace Robotiq {
namespace {
const char* toString(Logger::Level level)
{
   switch(level)
   {
   case Logger::Level::Debug:
      return "DEBUG";
   case Logger::Level::Info:
      return "INFO ";
   case Logger::Level::Warn:
      return "WARN ";
   case Logger::Level::Error:
      return "ERROR";
   }
   return "?????";
}

// Wall-clock timestamp with microsecond resolution, e.g.
// [2026-07-20 14:03:22.123456]
void writeTimestamp(std::ostream& out)
{
   const auto now = std::chrono::system_clock::now();
   const auto time = std::chrono::system_clock::to_time_t(now);
   const auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1000000;
   std::tm tmBuf{};
#if defined(_WIN32)
   localtime_s(&tmBuf, &time);
#else
   localtime_r(&time, &tmBuf);
#endif
   char buf[32];
   std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
   out << '[' << buf << '.' << std::setfill('0') << std::setw(6) << us.count() << "] ";
}

} // namespace

void Logger::logf(Level level, const char* fmt, ...)
{
   char buf[512];
   va_list args;
   va_start(args, fmt);
   vsnprintf(buf, sizeof(buf), fmt, args);
   va_end(args);
   log(level, buf);
}

void StderrLogger::log(Level level, std::string_view message)
{
   std::lock_guard<std::mutex> lock(_mutex);
   writeTimestamp(std::cerr);
   std::cerr << '[' << toString(level) << "] " << message << '\n';
}

void NullLogger::log(Level, std::string_view) {}

std::shared_ptr<Logger> makeDefaultLogger()
{
   // One shared instance: fallback users share a single mutex, so their
   // stderr lines stay serialized against each other.
   static const auto instance = std::make_shared<StderrLogger>();
   return instance;
}
} // namespace Robotiq
