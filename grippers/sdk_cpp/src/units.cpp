// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/gripper/units.hpp>

#include <algorithm>
#include <cmath>

namespace Robotiq::units {

namespace {
uint8_t roundedRegister(double steps)
{
   return static_cast<uint8_t>(std::lround(steps));
}
} // namespace

std::optional<uint8_t> registerFromSpan(double value, double minimum, double fullScale)
{
   if(!std::isfinite(value) || value < 0.0 || !std::isfinite(minimum) || !std::isfinite(fullScale)
      || fullScale <= minimum)
   {
      return std::nullopt;
   }
   const double fraction = std::clamp((value - minimum) / (fullScale - minimum), 0.0, 1.0);
   return roundedRegister(fraction * 0xFF);
}

std::optional<uint8_t> speedToRegister(double metresPerSecond, const DeviceProfile& profile)
{
   return registerFromSpan(metresPerSecond, profile.minSpeed, profile.fullScaleSpeed);
}

std::optional<uint8_t> forceToRegister(double newtons, const DeviceProfile& profile)
{
   return registerFromSpan(newtons, profile.minForce, profile.fullScaleForce);
}

std::optional<uint8_t> openingToRegister(double openingMetres, const DeviceProfile& profile)
{
   if(!std::isfinite(openingMetres) || !std::isfinite(profile.stroke) || profile.stroke <= 0.0
      || profile.registerBand() <= 0.0)
   {
      return std::nullopt;
   }
   const double closedFraction = 1.0 - std::clamp(openingMetres / profile.stroke, 0.0, 1.0);
   return roundedRegister(profile.openRegister + closedFraction * profile.registerBand());
}

std::optional<double> openingFromRegister(uint8_t value, const DeviceProfile& profile)
{
   const double band = profile.registerBand();
   if(band <= 0.0 || !std::isfinite(profile.stroke) || profile.stroke <= 0.0)
   {
      return std::nullopt;
   }
   const double closedFraction = std::clamp((value - profile.openRegister) / band, 0.0, 1.0);
   return (1.0 - closedFraction) * profile.stroke;
}

} // namespace Robotiq::units
