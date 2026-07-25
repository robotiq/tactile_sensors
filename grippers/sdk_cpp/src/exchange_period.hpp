// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Internal: ConnectionConfig::connectionFrequency -> exchange period.
//! Shared by the real and fake gripper factories so the two agree on
//! what a given frequency means.

#pragma once

#include <chrono>
#include <cmath>

#include "frequency.hpp"

namespace Robotiq::detail {

// Supported exchange frequency (Hz) to cycle period. Free-run becomes a zero
// period, which the exchange loop paces past immediately: on a real link the
// wire does the pacing.
inline std::chrono::microseconds exchangePeriodFromFrequency(double hz)
{
   const double supported = clampFrequency(hz);
   if(supported == 0.0)
   {
      return std::chrono::microseconds{0};
   }
   return std::chrono::microseconds{std::llround(1000000.0 / supported)};
}

} // namespace Robotiq::detail
