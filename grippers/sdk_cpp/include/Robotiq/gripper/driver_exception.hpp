// Copyright (c) 2023 PickNik, Inc.
// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! This is a custom exception thrown by the Driver.

#pragma once

#include <exception>
#include <string>

namespace Robotiq {
class DriverException : public std::exception
{
   std::string _what;

public:
   // Plain string concatenation, not <sstream>: stringstream drags in the whole
   // iostream + locale machinery (incl. wide-char printf), which is dead weight
   // — and unlinkable with newlib-nano — on a freestanding target.
   explicit DriverException(const std::string& description)
      : _what("DriverException: " + description + ".")
   {
   }

   DriverException(const DriverException& other)
      : _what(other._what)
   {
   }

   ~DriverException() override = default;

   // Disable copy constructors
   DriverException& operator=(const DriverException&) = delete;

   [[nodiscard]] const char* what() const throw() override { return _what.c_str(); }
};
} // namespace Robotiq
