// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// Deprecated compatibility shim, removed in the next major release.
// NamedBitArray is how the command block stores its packed action byte, not
// something an application composes against, so it moved to
// <Robotiq/detail/named_bit_array.hpp>, namespace Robotiq::detail.

#pragma once

#pragma message("Robotiq/gripper/named_bit_array.hpp is deprecated; use GripperCommand::action")

#include <Robotiq/detail/named_bit_array.hpp>

namespace Robotiq {
using detail::NamedBitArray;
} // namespace Robotiq
