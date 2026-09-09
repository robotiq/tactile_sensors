// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The per-model figures the SI conversions in units.hpp scale
//!        against: the speed and force range the manual gives for
//!        counts 0x00..0xFF, the stroke, and the register-count band
//!        that spans it. Gripper itself is model-agnostic; a profile is
//!        passed to the conversions.
//!
//!        The count band is measured, not specified. The manual gives
//!        0x00 and 0xFF as command endpoints only; on the bench a 2F-85
//!        commanded to 0 settles at gPO 3 and commanded to 255 (or 230)
//!        settles at 228..230 depending on the unit. Fill a new model's
//!        profile the same way: command both extremes on a gripper with
//!        nothing between the fingers and read gPO back.

#pragma once

#include <cstdint>

namespace Robotiq {

struct DeviceProfile
{
   double minSpeed; //!< m/s — rSP 0x00
   double fullScaleSpeed; //!< m/s — rSP 0xFF
   double minForce; //!< N — rFR 0x00
   double fullScaleForce; //!< N — rFR 0xFF
   double stroke; //!< m — opening at openCount
   uint8_t openCount; //!< gPO after commanding rPR 0, measured
   uint8_t closedCount; //!< gPO after commanding rPR 255, measured

   //! Counts from full opening to full closure.
   [[nodiscard]] constexpr double countBand() const { return static_cast<double>(closedCount) - openCount; }
};

namespace profiles {
inline constexpr DeviceProfile k2F85{0.020, 0.150, 20.0, 235.0, 0.085, 3, 230};
} // namespace profiles

} // namespace Robotiq
