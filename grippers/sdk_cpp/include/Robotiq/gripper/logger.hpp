// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <memory>
#include <string_view>

namespace Robotiq {

//! \ingroup logging
//! \brief Minimal logging surface for Robotiq components.
//!
//! Abstracts logging so the SDK can be used in three contexts:
//!   - Production ROS 2 nodes — adapter forwards to rclcpp's logger
//!   - Standalone CLI / bench tools — default stderr printer (no rclcpp dep)
//!   - Unit tests — null logger or string capture
//!
//! The interface is tiny by design: one method taking a level and a
//! fully formatted message — formatting is the caller's concern.
//!
//! \par Example — a custom sink
//! \snippet snippets.cpp uart-logger
class Logger
{
public:
   //! Severity of a single log line.
   enum class Level
   {
      Debug, //!< high-frequency, diagnostic detail
      Info, //!< normal operational events
      Warn, //!< recoverable, but worth a human's attention
      Error, //!< an operation failed
   };

   virtual ~Logger() = default;

   //! \brief Emit a fully-formatted log line.
   //! \param level Severity of this line.
   //! \param message The already-formatted text to log. The sink decides
   //!        where it goes (stderr, UART, rclcpp, ...); it does not
   //!        format, timestamp, or filter by level.
   virtual void log(Level level, std::string_view message) = 0;
};

//! \ingroup logging
//! \brief A do-nothing Logger, useful in tight benchmarks or tests.
class NullLogger : public Logger
{
public:
   //! Discards \p message.
   void log(Level, std::string_view) override;
};

//! \ingroup logging
//! \brief Build the default logger used when callers don't inject one.
//!
//! StderrLogger on a hosted runtime, NullLogger on a freestanding target
//! (no console — pass an application Logger, e.g. a UART sink, to get
//! real logs there). The choice is made at build time by which TU is
//! compiled in.
//! \return A shared, ready-to-use default Logger.
std::shared_ptr<Logger> makeDefaultLogger();

} // namespace Robotiq
