// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Conversions between SI quantities and the 0..255 counts of the
//!        command and status blocks, scaled by a DeviceProfile. Every
//!        mapping here is linear and the gripper is not: the manual
//!        gives the 2F speed and force scales as approximate, force also
//!        varies with speed and payload (about ±10 %), and a count is a
//!        setpoint, not a measurement.
//!
//!        Speed and force map [minimum, fullScale] onto counts 0..255,
//!        because count 0 is the gripper's minimum, not zero. A value
//!        below the minimum floors at count 0 and a value above full
//!        scale saturates at 255. NaN, infinity and a negative value
//!        yield nothing: no benign caller produces them.
//!
//!        Opening maps [0, stroke] onto the profile's count band and
//!        clamps to the stroke ends rather than rejecting, because
//!        position setpoints from interpolating controllers overshoot
//!        the ends by a hair and holding the previous command there
//!        would be the wrong outcome. Counts round to nearest, so inside
//!        the band a position read back commands the same count. A
//!        closed gripper settles a few counts either side of
//!        closedCount, so a reading there converts to a small non-zero
//!        opening; counts past the band read as the nearest stroke end.
//!
//!        A profile the arithmetic cannot use (non-positive stroke or
//!        count band, a full scale at or below its minimum) yields
//!        nothing.

#pragma once

#include <cstdint>
#include <optional>

#include <Robotiq/gripper/device_profile.hpp>

namespace Robotiq::units {

//! The manual's approximate current equivalent for the 2F gCU byte.
inline constexpr double kAmperesPerCurrentCount = 0.010;

//! Linear map of [minimum, fullScale] onto counts 0..255, the rule
//! speedToCount and forceToCount apply. Public for callers that carry
//! their own scales instead of a DeviceProfile.
[[nodiscard]] std::optional<uint8_t> countFromSpan(double value, double minimum, double fullScale);

//! Speed in m/s -> rSP.
[[nodiscard]] std::optional<uint8_t> speedToCount(double metresPerSecond, const DeviceProfile& profile);

//! Force in N -> rFR.
[[nodiscard]] std::optional<uint8_t> forceToCount(double newtons, const DeviceProfile& profile);

//! Opening in m -> rPR.
[[nodiscard]] std::optional<uint8_t> openingToCount(double openingMetres, const DeviceProfile& profile);

//! gPO -> opening in m.
[[nodiscard]] std::optional<double> openingFromCount(uint8_t count, const DeviceProfile& profile);

//! gCU -> motor current in A.
[[nodiscard]] inline constexpr double motorCurrentFromCount(uint8_t count)
{
   return kAmperesPerCurrentCount * count;
}

} // namespace Robotiq::units
