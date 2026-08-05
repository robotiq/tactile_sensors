// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Minimal logging surface for Robotiq components.
//! Abstracts logging so the SDK can be used in three contexts:
//!   - Production ROS 2 nodes — adapter forwards to rclcpp's logger
//!   - Standalone CLI / bench tools — default stderr printer (no rclcpp dep)
//!   - Unit tests — null logger or string capture
//! The interface is tiny by design: one method taking a level and a
//! fully formatted message — formatting is the caller's concern.

#pragma once

#include <memory>
#include <string_view>

namespace Robotiq {

class Logger
{
public:
   enum class Level
   {
      Debug,
      Info,
      Warn,
      Error,
   };

   virtual ~Logger() = default;

   // Emit a fully-formatted log line. Implementation-defined sink.
   virtual void log(Level level, std::string_view message) = 0;
};

//! \brief A do-nothing Logger, useful in tight benchmarks or tests.
class NullLogger : public Logger
{
public:
   void log(Level, std::string_view) override;
};

// Build the default logger used when callers don't inject one: StderrLogger
// on a hosted runtime, NullLogger on a freestanding target (no console —
// pass an application Logger, e.g. a UART sink, to get real logs). The
// choice is made at build time by which TU is compiled in.
std::shared_ptr<Logger> makeDefaultLogger();

} // namespace Robotiq
