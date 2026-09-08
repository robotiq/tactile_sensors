// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Conversions between SI quantities and the 0..255 register
//!        counts of the command and status blocks. Speed, force and
//!        opening scale by a DeviceProfile; motor current is a fixed
//!        10 mA per count on every model. The profile-scaled functions
//!        are pure and yield nothing for a value the arithmetic cannot
//!        turn into a count: NaN, infinity, a negative speed or force,
//!        a profile with a zero full scale or count band. Values past
//!        full scale saturate; openings past either end of the stroke
//!        clamp to the mechanical range. Counts round to nearest, so a
//!        position read back commands the same count.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include <Robotiq/gripper/device_profile.hpp>

namespace Robotiq {

inline constexpr double kAmperesPerCurrentCount = 0.010;

namespace detail {
[[nodiscard]] inline uint8_t roundedCount(double counts)
{
   return static_cast<uint8_t>(std::lround(counts));
}

[[nodiscard]] inline std::optional<uint8_t> registerFromFractionOf(double value, double fullScale)
{
   if(!std::isfinite(value) || value < 0.0 || !std::isfinite(fullScale) || fullScale <= 0.0)
   {
      return std::nullopt;
   }
   return roundedCount(std::min(value / fullScale, 1.0) * 0xFF);
}

[[nodiscard]] inline double countBand(const DeviceProfile& profile)
{
   return static_cast<double>(profile.closedCount) - profile.openCount;
}
} // namespace detail

//! Speed in m/s -> rSP.
[[nodiscard]] inline std::optional<uint8_t> speedRegister(double metresPerSecond, const DeviceProfile& profile)
{
   return detail::registerFromFractionOf(metresPerSecond, profile.fullScaleSpeed);
}

//! Force in N -> rFR.
[[nodiscard]] inline std::optional<uint8_t> forceRegister(double newtons, const DeviceProfile& profile)
{
   return detail::registerFromFractionOf(newtons, profile.fullScaleForce);
}

//! Opening in m -> rPR, linear over the profile's count band.
[[nodiscard]] inline std::optional<uint8_t> openingRegister(double openingMetres, const DeviceProfile& profile)
{
   if(!std::isfinite(openingMetres) || !std::isfinite(profile.stroke) || profile.stroke <= 0.0)
   {
      return std::nullopt;
   }
   const double closedFraction = 1.0 - std::clamp(openingMetres / profile.stroke, 0.0, 1.0);
   return detail::roundedCount(profile.openCount + closedFraction * detail::countBand(profile));
}

//! gPO -> opening in m, linear over the profile's count band; counts
//! outside the band read as the nearest stroke end.
[[nodiscard]] inline std::optional<double> openingFromRegister(uint8_t count, const DeviceProfile& profile)
{
   const double band = detail::countBand(profile);
   if(band == 0.0)
   {
      return std::nullopt;
   }
   const double closedFraction = std::clamp((count - profile.openCount) / band, 0.0, 1.0);
   return (1.0 - closedFraction) * profile.stroke;
}

//! gCU -> motor current in A.
[[nodiscard]] inline constexpr double motorCurrentFromRegister(uint8_t count)
{
   return kAmperesPerCurrentCount * count;
}

} // namespace Robotiq
