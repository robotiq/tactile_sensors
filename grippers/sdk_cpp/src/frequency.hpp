// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include <Robotiq/gripper/driver_exception.hpp>

namespace Robotiq::detail {

inline constexpr double kMinFrequency = 0.1; // Hz
inline constexpr double kMaxFrequency = 1000.0; // Hz

//! Fold a requested exchange frequency into the supported range. Below the
//! floor, procedures that wait on status stall for no reason; above the
//! ceiling, no supported baud rate can carry the requests anyway.
//! \p hz of 0 passes through as 0, meaning free-run.
//! \throw DriverException if \p hz is negative or NaN.
inline double clampFrequency(double hz)
{
   if(std::isnan(hz) || hz < 0.0)
   {
      throw DriverException("exchange frequency must be a non-negative number of hertz; got " + std::to_string(hz));
   }
   if(hz == 0.0)
   {
      return 0.0;
   }
   return std::clamp(hz, kMinFrequency, kMaxFrequency);
}
} // namespace Robotiq::detail
