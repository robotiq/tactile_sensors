// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <cstdint>
#include <optional>

#include <Robotiq/gripper/device_profile.hpp>

namespace Robotiq::units {

//! \cond DOXYGEN_EXCLUDE
// The manual's approximate current equivalent for the 2F gCU byte.
inline constexpr double kAmperesPerRegisterStep = 0.010;
//! \endcond

//! \ingroup units
//! Linear map of [minimum, maximum] onto register values 0..255, the rule
//! speedToRegister applies. Public for callers that carry
//! their own scales instead of a DeviceProfile.
//! \param value The quantity to convert. A value below \p minimum floors
//!        at register 0; a value above \p maximum saturates at 255.
//! \param minimum The value that maps to register 0.
//! \param maximum The value that maps to register 255.
//! \return The mapped register, or std::nullopt if \p value is
//!         non-finite or negative, \p minimum or \p maximum is
//!         non-finite, or \p maximum is at or below \p minimum.
[[nodiscard]] std::optional<uint8_t> registerFromSpan(double value, double minimum, double maximum);

//! \ingroup units
//! Speed in m/s -> rSP.
//! \param speed Requested speed, in m/s. A value below profile.minSpeed
//!        floors at register 0; a value above profile.maxSpeed
//!        saturates at 255.
//! \param profile The device profile to scale against.
//! \return rSP, or std::nullopt if \p speed is non-finite or
//!         negative, or \p profile is one the arithmetic can't use
//!         (maxSpeed at or below minSpeed).
[[nodiscard]] std::optional<uint8_t> speedToRegister(double speed, const DeviceProfile& profile);

//! \ingroup units
//! Opening in m -> rPR.
//! \warning Valid only while the gripper stays in parallel-finger mode:
//!          profile.minOpening/maxOpening and the register endpoints are
//!          measured in that mode, so a register computed here does not
//!          correspond to a real opening once the gripper switches to
//!          encompassing mode.
//! \param opening Target opening, in metres. Clamped to
//!        [profile.minOpening, profile.maxOpening] rather than
//!        rejected — a value outside that range reads as the nearest
//!        end. Values round to the nearest register.
//! \param profile The device profile to scale against.
//! \return rPR, or std::nullopt if \p opening is non-finite, or
//!         \p profile is one the arithmetic can't use (non-positive
//!         position span or register band).
[[nodiscard]] std::optional<uint8_t> openingToRegister(double opening, const DeviceProfile& profile);

//! \ingroup units
//! gPO -> opening in m.
//! \warning Valid only while the gripper stays in parallel-finger mode. A
//!          gPO reading taken in encompassing mode can exceed
//!          profile.closedPosition and is clamped to the nearest end
//!          below instead of reflecting the real opening.
//! \param value The raw register value (gPO). A value outside the
//!        profile's register band reads as the nearest end.
//! \param profile The device profile to scale against.
//! \return The opening in metres, or std::nullopt if \p profile is one
//!         the arithmetic can't use (non-positive position span or
//!         register band).
[[nodiscard]] std::optional<double> openingFromRegister(uint8_t value, const DeviceProfile& profile);

//! \ingroup units
//! Effort in [0, 1] -> rFR. 0 is the gripper's minimum force, 1 its
//! maximum; a value above 1 saturates at register 255.
//! \param effort Requested gripping effort, as a fraction of the
//!        gripper's force range.
//! \return rFR, or std::nullopt if \p effort is non-finite or negative.
[[nodiscard]] std::optional<uint8_t> effortToRegister(double effort);

//! \ingroup units
//! gCU -> motor current in A.
//! \param value The raw register value (gCU).
//! \return The motor current in amperes.
[[nodiscard]] inline constexpr double motorCurrentFromRegister(uint8_t value)
{
   return kAmperesPerRegisterStep * value;
}

} // namespace Robotiq::units
