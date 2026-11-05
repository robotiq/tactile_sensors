// Copyright (c) 2023 PickNik, Inc.
// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <exception>
#include <string>

namespace Robotiq {

//! \ingroup exceptions
//! \brief A higher-level driver problem that should propagate up to the
//! application — invalid configuration, a missing/unresponsive gripper,
//! or a null argument the caller must not have passed.
//!
//! See SerialIOException for wire-level failures
class DriverException : public std::exception
{
   std::string _what;

public:
   //! \param description What went wrong; wrapped as "DriverException: <description>.".
   // Plain string concatenation, not <sstream>: stringstream drags in the whole
   // iostream + locale machinery (incl. wide-char printf), which is dead weight
   // — and unlinkable with newlib-nano — on a freestanding target.
   explicit DriverException(const std::string& description)
      : _what("DriverException: " + description + ".")
   {
   }

   //! \param other The exception to copy.
   DriverException(const DriverException& other)
      : _what(other._what)
   {
   }

   ~DriverException() override = default;

   DriverException& operator=(const DriverException&) = delete;

   //! \return The formatted "DriverException: <description>." message.
   [[nodiscard]] const char* what() const throw() override { return _what.c_str(); }
};
} // namespace Robotiq
