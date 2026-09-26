// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! What the examples share: connecting with a checklist on failure,
//! activating with fault recovery, and a move that waits for its outcome.
//! See the robust example walkthrough in docs/.

#pragma once

#include <memory>
#include <string>

namespace Robotiq {
class Gripper;
class Logger;
struct ConnectionConfig;
struct DeviceProfile;
struct GripperCommand;
} // namespace Robotiq

namespace examples {

// \p message followed by "; link=<state> <decoded status>", for log lines
// and error reports.
std::string withStatus(std::string message, Robotiq::Gripper& gripper);

// Whether the fingers have stopped: on an object, or at the requested
// position.
bool motionSettled(Robotiq::Gripper& gripper);

// Open the port \p config names and start exchanging; on failure, print
// what to check on stderr and return null.
std::unique_ptr<Robotiq::Gripper> connectGripper(const Robotiq::ConnectionConfig& config);

// Activate, recovering from a latched fault first (the fingers move);
// false when activation failed or timed out.
bool activateOrRecover(Robotiq::Gripper& gripper, Robotiq::Logger& logger);

// Send the gripper to \p openingMetres (\p profile's minOpening = fully
// closed, maxOpening = fully open) and block until it gets there: the
// request echoed, the motion seen, the motion settled. Speed and force are
// whatever \p command already holds.
bool moveTo(Robotiq::Gripper& gripper,
            Robotiq::GripperCommand& command,
            double openingMetres,
            const Robotiq::DeviceProfile& profile,
            Robotiq::Logger& logger);

} // namespace examples
