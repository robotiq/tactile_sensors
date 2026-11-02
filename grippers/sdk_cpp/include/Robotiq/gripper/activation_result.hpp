// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

namespace Robotiq {
//! \ingroup activation
//! Result of the blocking activation procedures activate() and recoverFromFault().
enum class ActivationResult
{
   Activated, //!< the gripper reports activation complete — the handshake
              //!< ran, or one already under way finished
   AlreadyActive, //!< already activated and fault-free; nothing was sent
   FaultLatched, //!< a major fault is latched; activate() refuses the reset
   Timeout, //!< the link stayed down, completion never arrived in time, or
            //!< too little of the timeout remained to run the handshake
};
} // namespace Robotiq
