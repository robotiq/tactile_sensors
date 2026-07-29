// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Connection parameters for a gripper: the serial link plus the
//!        Modbus addressing.
//! Plain aggregate so the SDK can be configured without any framework:
//! a ROS 2 wrapper fills it from hardware parameters, a CLI from argv.

#pragma once

#include <cstdint>

#include <Robotiq/gripper/serial_config.hpp>

namespace Robotiq {

// The gripper's factory-set Modbus slave address. Named so callers and
// tests can reference the default without constructing a ConnectionConfig,
// which is not a literal type (SerialConfig holds a std::string).
inline constexpr uint8_t kDefaultModbusSlaveAddress = 0x09;

struct ConnectionConfig
{
   SerialConfig serial;

   uint8_t modbusSlaveAddress = kDefaultModbusSlaveAddress;

   // Frequency of the background exchange cycle (Gripper), in hertz; 0 =
   // free-run (exchange as fast as the bus allows). The default is
   // conservative for 115200 baud (one FC 0x17 exchange takes ~4 ms
   // median, ~5 ms p99). Above ~1000 Hz the request rate exceeds what any
   // supported baud rate can carry, so the cycle degenerates into
   // free-run; treat 1000 Hz as the useful upper bound.
   double connectionFrequency = 100.0; // Hz
};
} // namespace Robotiq
