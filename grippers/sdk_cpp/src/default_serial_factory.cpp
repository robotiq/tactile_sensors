// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// The ConnectionConfig constructors — the desktop convenience path that
// builds a libserialport DefaultSerial from a port name. Compiled only when
// GRIPPERS_BUILD_DEFAULT_SERIAL is ON, keeping the core free of the
// dependency; other targets inject their own Serial.

#include <memory>
#include <utility>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/detail/default_serial.hpp>
#include <Robotiq/detail/gripper_modbus_client.hpp>
#include <Robotiq/gripper/serial.hpp>

#include "exchange_period.hpp"

namespace Robotiq {
namespace {
std::unique_ptr<Serial> makeSerial(const ConnectionConfig& config, const std::shared_ptr<Logger>& logger)
{
   return std::make_unique<detail::DefaultSerial>(config.serial, logger);
}
} // namespace

Gripper::Gripper(const ConnectionConfig& config, std::shared_ptr<Logger> logger)
   : Gripper(makeSerial(config, logger),
             config.modbusSlaveAddress,
             detail::exchangePeriodFromFrequency(config.connectionFrequency),
             makeDefaultPlatform(),
             logger)
{
}

namespace detail {
GripperModbusClient::GripperModbusClient(const ConnectionConfig& config, std::shared_ptr<Logger> logger)
   : GripperModbusClient(makeSerial(config, logger), config.modbusSlaveAddress, logger)
{
}
} // namespace detail
} // namespace Robotiq
