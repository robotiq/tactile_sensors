// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// Deprecated compatibility shim, removed in the next major release.
// GripperModbusClient is supported public API — it never belonged under
// detail/ — and now lives in <Robotiq/gripper/modbus_client.hpp>,
// namespace Robotiq. Read its warning before reaching for it.

#pragma once

#pragma message("Robotiq/detail/gripper_modbus_client.hpp is deprecated; use <Robotiq/gripper/modbus_client.hpp>")

#include <Robotiq/gripper/modbus_client.hpp>

namespace Robotiq::detail {
using GripperModbusClient = ::Robotiq::GripperModbusClient;
} // namespace Robotiq::detail
