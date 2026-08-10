// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// StderrLogger and the hosted makeDefaultLogger. Freestanding builds leave
// this TU out (no console; <iostream> pulls in wide-char printf and ~25 KB,
// and the wall clock needs _gettimeofday) and compile
// freestanding/default_logger.cpp instead.

#include <Robotiq/gripper/stderr_logger.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <utility>

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

StderrLogger::StderrLogger(std::string name)
   : _name(std::move(name))
{
}

void StderrLogger::log(Level level, std::string_view message)
{
   // All instances share the stream, so they share one mutex.
   static std::mutex mutex;
   const std::lock_guard<std::mutex> lock(mutex);
   writeTimestamp(std::cerr);
   std::cerr << '[' << toString(level) << "] ";
   if(!_name.empty())
   {
      std::cerr << '[' << _name << "] ";
   }
   std::cerr << message << '\n';
}

std::shared_ptr<Logger> makeDefaultLogger()
{
   static const auto instance = std::make_shared<StderrLogger>();
   return instance;
}
} // namespace Robotiq
