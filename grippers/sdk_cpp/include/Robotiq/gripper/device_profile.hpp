// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The per-model specification the SI conversions in units.hpp
//!        scale against: full-scale speed and force, the stroke, and
//!        the register-count band that spans it. The gripper accepts
//!        0..255 but the extremes sit outside the mechanical range, so
//!        the opening mapping is anchored on the band. Gripper itself
//!        is model-agnostic; a profile is passed to the conversions.

#pragma once

#include <cstdint>

namespace Robotiq {

struct DeviceProfile
{
   double fullScaleSpeed; //!< m/s — rSP 0xFF
   double fullScaleForce; //!< N — rFR 0xFF
   double stroke; //!< m — opening at openCount
   uint8_t openCount; //!< rPR/gPO at full opening
   uint8_t closedCount; //!< rPR/gPO at full closure
};

namespace profiles {
inline constexpr DeviceProfile k2F85{0.150, 235.0, 0.085, 3, 230};
} // namespace profiles

} // namespace Robotiq
