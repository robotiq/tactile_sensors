// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/gripper/units.hpp>

#include <algorithm>
#include <cmath>

namespace Robotiq::units {

namespace {
uint8_t roundedCount(double counts)
{
   return static_cast<uint8_t>(std::lround(counts));
}
} // namespace

std::optional<uint8_t> countFromSpan(double value, double minimum, double fullScale)
{
   if(!std::isfinite(value) || value < 0.0 || !std::isfinite(minimum) || !std::isfinite(fullScale)
      || fullScale <= minimum)
   {
      return std::nullopt;
   }
   const double fraction = std::clamp((value - minimum) / (fullScale - minimum), 0.0, 1.0);
   return roundedCount(fraction * 0xFF);
}

std::optional<uint8_t> speedToCount(double metresPerSecond, const DeviceProfile& profile)
{
   return countFromSpan(metresPerSecond, profile.minSpeed, profile.fullScaleSpeed);
}

std::optional<uint8_t> forceToCount(double newtons, const DeviceProfile& profile)
{
   return countFromSpan(newtons, profile.minForce, profile.fullScaleForce);
}

std::optional<uint8_t> openingToCount(double openingMetres, const DeviceProfile& profile)
{
   if(!std::isfinite(openingMetres) || !std::isfinite(profile.stroke) || profile.stroke <= 0.0
      || profile.countBand() <= 0.0)
   {
      return std::nullopt;
   }
   const double closedFraction = 1.0 - std::clamp(openingMetres / profile.stroke, 0.0, 1.0);
   return roundedCount(profile.openCount + closedFraction * profile.countBand());
}

std::optional<double> openingFromCount(uint8_t count, const DeviceProfile& profile)
{
   const double band = profile.countBand();
   if(band <= 0.0 || !std::isfinite(profile.stroke) || profile.stroke <= 0.0)
   {
      return std::nullopt;
   }
   const double closedFraction = std::clamp((count - profile.openCount) / band, 0.0, 1.0);
   return (1.0 - closedFraction) * profile.stroke;
}

} // namespace Robotiq::units
