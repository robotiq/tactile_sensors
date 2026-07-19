// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Thrown by Serial implementations when an IO operation fails in a
//!        way that may be transient (timeout, short read/write, EINTR, etc.).
//! Distinct from DriverException, which signals a higher-level driver problem
//! that should propagate up. SerialIOException is caught by the Driver
//! implementation to drive its retry loop.

#pragma once

#include <stdexcept>
#include <string>

namespace Robotiq {
class SerialIOException : public std::runtime_error
{
public:
   explicit SerialIOException(const std::string& description)
      : std::runtime_error("SerialIOException: " + description)
   {
   }
};
} // namespace Robotiq
