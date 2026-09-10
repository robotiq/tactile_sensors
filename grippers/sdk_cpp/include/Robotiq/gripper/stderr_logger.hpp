// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <Robotiq/detail/config.hpp>

#if !GRIPPERS_HOSTED
#error "StderrLogger is hosted-only: on a freestanding target, implement a Logger over your own sink instead."
#endif

#include <string>
#include <string_view>

#include <Robotiq/gripper/logger.hpp>

namespace Robotiq {

//! \ingroup logging
//! \brief Default Logger implementation that writes to stderr.
//! Hosted-only: it needs \<iostream\> and a wall clock, neither of which a
//! freestanding target has — provide an application Logger (e.g. a UART
//! sink) there instead.
//!
//! Thread-safe: writes are serialized across all instances, so they
//! share the stream without garbling lines. The optional name tags
//! every line: inject one instance into the SDK and keep another for
//! application messages to tell them apart (a real integration gets the
//! same separation from its injected adapter, e.g. a named rclcpp
//! logger).
class StderrLogger : public Logger
{
public:
   //! \param name Optional tag prepended to every line this instance
   //!        writes; empty by default (no tag).
   explicit StderrLogger(std::string name = {});

   void log(Level level, std::string_view message) override;

private:
   std::string _name;
};

} // namespace Robotiq
