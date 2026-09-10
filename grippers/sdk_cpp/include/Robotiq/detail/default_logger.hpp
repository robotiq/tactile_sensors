// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The logger the SDK falls back to when a caller injects none.
//! Internal: applications pass their own Logger, or a null pointer to get
//! this one. StderrLogger on a hosted runtime, NullLogger on a freestanding
//! target (no console — pass an application Logger, e.g. a UART sink, to
//! get real logs).

#pragma once

#include <memory>

namespace Robotiq {
class Logger;
} // namespace Robotiq

namespace Robotiq::detail {

[[nodiscard]] std::shared_ptr<Logger> makeDefaultLogger();

} // namespace Robotiq::detail
