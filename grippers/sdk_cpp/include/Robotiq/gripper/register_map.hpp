// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// Deprecated compatibility shim, removed in the next major release. The
// register map describes how the typed command and status blocks are laid
// out; it is not something an application composes against, so it moved to
// <Robotiq/detail/register_map.hpp>, namespace Robotiq::detail::register_map.

#pragma once

#pragma message("Robotiq/gripper/register_map.hpp is deprecated; use <Robotiq/detail/register_map.hpp>")

#include <Robotiq/detail/register_map.hpp>

namespace Robotiq::register_map {
using namespace ::Robotiq::detail::register_map;
} // namespace Robotiq::register_map
