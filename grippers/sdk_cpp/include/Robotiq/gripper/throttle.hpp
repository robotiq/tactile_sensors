// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// Deprecated compatibility shim, removed in the next major release.
// Throttle rate-limits the SDK's own log lines, so it moved to
// <Robotiq/detail/throttle.hpp>, namespace Robotiq::detail.

#pragma once

#pragma message("Robotiq/gripper/throttle.hpp is deprecated; Throttle is internal to the SDK")

#include <Robotiq/detail/throttle.hpp>

namespace Robotiq {
using detail::Throttle;
} // namespace Robotiq
