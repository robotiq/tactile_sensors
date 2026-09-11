// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <cstdint>

namespace Robotiq {

//! \ingroup units
//! \brief Gripper model specific profile gathering specifications such as
//!        opening range and speed.
//!
//! openPosition and closedPosition are measured, not taken from the
//! manual: with nothing between the fingers, command rPR 0 and read gPO
//! back, then command rPR 255 and read gPO back. Do this on your own
//! gripper when filling a new profile, especially with custom fingers.
//!
//! \warning Position <-> register conversion are valid only while
//!          the gripper stays in parallel-finger mode. In encompassing
//!          mode gPO can go past closedPosition; this profile has no way
//!          to represent that, so a reading taken in encompassing mode
//!          does not convert to a meaningful opening.
//!
//! \warning minOpening, maxOpening, openPosition and closedPosition
//!          depend on which fingers are installed on the gripper:
//!          swapping fingers changes the physical opening range, and
//!          can shift the measured register endpoints too.
struct DeviceProfile
{
   double minSpeed; //!< m/s
   double maxSpeed; //!< m/s
   double minOpening; //!< m — opening at closedPosition
   double maxOpening; //!< m — opening at openPosition
   uint8_t openPosition; //!< gPO after commanding rPR 0, measured
   uint8_t closedPosition; //!< gPO after commanding rPR 255, measured

   //! Register steps from full opening to full closure, in parallel mode.
   [[nodiscard]] constexpr double registerPositionRange() const
   {
      return static_cast<double>(closedPosition) - openPosition;
   }

   //! Metres from full closure to full opening.
   [[nodiscard]] constexpr double openingRange() const { return maxOpening - minOpening; }
};

namespace profiles {
//! \ingroup units
//! \brief The measured profile for the 2F-85
//!
//! \warning Assumes that the gripper is in parallel-finger mode.
inline constexpr DeviceProfile k2F85{0.020, 0.150, 0.0, 0.085, 3, 230};

} // namespace profiles

} // namespace Robotiq
