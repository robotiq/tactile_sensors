// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <memory>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/logger.hpp>

namespace Robotiq {

//! \ingroup testing
//! \brief Build a Gripper for testing and CI, backed by an in-process
//! fake device instead of real hardware.
//!
//! The returned Gripper is the real one, driving a real Modbus client,
//! against an in-process device that implements the documented register
//! map. Everything above the wire — the typed blocks, the exchange
//! cycle, the process image, activate()/recoverFromFault() — behaves
//! exactly as against hardware.
//!
//! The fake device is deliberately minimal, and matches what integrators
//! expect of a dummy gripper: activation completes instantly, and the
//! fingers are wherever they were last commanded to be. There is no
//! motion profile, no travel time, no object detection and no fault
//! injection.
//!
//! \param config Only modbusSlaveAddress and connectionFrequency apply; the
//!        serial settings are ignored, as there is no port to configure.
//!        connectionFrequency is clamped to [0.1, 1000] Hz — a real link
//!        paces itself against the wire, a fake one has to be bounded —
//!        and 0, meaning free-run, gets the top of that range. Clamping is
//!        logged.
//! \param logger Log sink; pass null to use the default stderr logger.
//! \return A Gripper driving the in-process fake device.
//! \throw DriverException if the fake device cannot be created, or if
//!        connectionFrequency is negative.
//!
//! \par Example
//! \snippet snippets.cpp make-fake-gripper
[[nodiscard]] std::unique_ptr<Gripper> makeFakeGripper(const ConnectionConfig& config = {},
                                                       std::shared_ptr<Logger> logger = nullptr);
} // namespace Robotiq
