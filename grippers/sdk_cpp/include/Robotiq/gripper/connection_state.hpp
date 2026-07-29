// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

namespace Robotiq {
enum class ConnectionState
{
   // Reserved: the link is closed or lost.
   Disconnected,
   // Will be used for automatic reconnection.
   Connecting,
   // Exchanges are succeeding; the process image is live.
   Operational,
   // Several consecutive exchanges failed (e.g. gripper unplugged);
   // recovers to Operational automatically on the next success.
   Faulted,
};
} // namespace Robotiq
