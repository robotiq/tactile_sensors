// Copyright (c) 2023 PickNik, Inc.
// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! This is a custom exception thrown by the Driver.

#pragma once

#include <exception>
#include <string>
#include <sstream>

namespace Robotiq {
class DriverException : public std::exception
{
   std::string _what;

public:
   explicit DriverException(const std::string& description)
   {
      std::stringstream ss;
      ss << "DriverException: " << description << ".";
      _what = ss.str();
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
