// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/gripper/logger.hpp>

namespace Robotiq {
void NullLogger::log(Level, std::string_view) {}
} // namespace Robotiq
