// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

namespace Robotiq {
//! \ingroup connection
//! The state of a Gripper's background exchange cycle; see Gripper::connectionState().
enum class ConnectionState
{
   Disconnected, //!< reserved: the link is closed or lost
   Connecting, //!< reserved: will be used for automatic reconnection
   Operational, //!< exchanges are succeeding; the process image is live
   Faulted, //!< several consecutive exchanges failed (e.g. gripper unplugged);
            //!< recovers to Operational automatically on the next success
};
} // namespace Robotiq
