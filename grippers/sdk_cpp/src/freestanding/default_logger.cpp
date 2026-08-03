// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// makeDefaultLogger for freestanding targets: no console, so the default
// discards. Pass an application Logger (e.g. a UART sink) to the
// constructors to get real logs.

#include <memory>

#include <Robotiq/gripper/logger.hpp>

namespace Robotiq {
std::shared_ptr<Logger> makeDefaultLogger()
{
   static const std::shared_ptr<Logger> instance = std::make_shared<NullLogger>();
   return instance;
}
} // namespace Robotiq
