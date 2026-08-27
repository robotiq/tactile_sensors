// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// The ConnectionConfig constructors — the desktop convenience path that
// builds a libserialport DefaultSerial from a port name. Compiled only when
// GRIPPERS_BUILD_DEFAULT_SERIAL is ON, keeping the core free of the
// dependency; other targets inject their own Serial.

#include <cstdint>
#include <memory>
#include <string>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/detail/default_serial.hpp>
#include <Robotiq/detail/gripper_modbus_client.hpp>
#include <Robotiq/gripper/serial.hpp>

#include "exchange_period.hpp"

namespace Robotiq {
namespace {
std::string toHex(uint8_t value)
{
   static const char* kDigits = "0123456789ABCDEF";
   return std::string("0x") + kDigits[(value >> 4) & 0x0F] + kDigits[value & 0x0F];
}

std::unique_ptr<Serial> makeSerial(const ConnectionConfig& config, const std::shared_ptr<Logger>& logger)
{
   // Logged before the link is opened, so a connection that never succeeds
   // still says what it was attempting.
   const std::shared_ptr<Logger> sink = logger ? logger : makeDefaultLogger();
   sink->log(Logger::Level::Info,
             "connecting to a gripper on " + config.serial.port + " at " + std::to_string(config.serial.baudrate)
                + " bps, Modbus slave address " + toHex(config.modbusSlaveAddress));

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
