// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Bounds on the exchange rate of a fake gripper.
//! A real link paces itself against the wire. A fake device answers in
//! microseconds, so the bounds have to be imposed: below the floor procedures
//! that wait on status would stall for no reason, and above the ceiling the
//! exchange cycle stops pacing and starts spinning a core.

#pragma once

#include <algorithm>
#include <string>

#include <Robotiq/gripper/driver_exception.hpp>

namespace Robotiq::fake {

inline constexpr double kMinFrequency = 0.1; // Hz
inline constexpr double kMaxFrequency = 1000.0; // Hz

//! Clamp a requested frequency into the range a fake gripper supports.
//! 0 means free-run — "as fast as the link allows", which without a link is
//! kMaxFrequency, matching how a real gripper behaves when unpaced.
//! \throw DriverException if \p hz is negative: there is no rate that could
//!        mean, so it is a bug in the caller rather than a value to absorb.
inline double clampFrequency(double hz)
{
   if(hz < 0.0)
   {
      throw DriverException("negative exchange frequency requested: " + std::to_string(hz) + " Hz");
   }
   if(hz == 0.0)
   {
      return kMaxFrequency;
   }
   return std::clamp(hz, kMinFrequency, kMaxFrequency);
}
} // namespace Robotiq::fake
