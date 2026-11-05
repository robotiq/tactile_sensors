// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <cstdint>

#include <Robotiq/gripper/serial_config.hpp>

namespace Robotiq {

//! \ingroup connection
//! \brief The gripper's factory-set Modbus slave address.
inline constexpr uint8_t kDefaultModbusSlaveAddress = 0x09;

//! \ingroup connection
//! \brief Connection parameters for a gripper: the serial link plus the
//! Modbus addressing.
//!
//! Plain aggregate so the SDK can be configured without any framework: a
//! ROS 2 wrapper fills it from hardware parameters, a CLI from argv.
//!
//! \par Example
//! \snippet snippets.cpp configure-connection
struct ConnectionConfig
{
   //! \brief Serial link parameters (port, baud rate, timeouts).
   SerialConfig serial;

   //! \brief Modbus slave address of the gripper on the bus.
   uint8_t modbusSlaveAddress = kDefaultModbusSlaveAddress;

   //! \brief Frequency of the background exchange cycle (Gripper), in hertz.
   //!
   //! 0 means free-run (exchange as fast as the bus allows). The default
   //! is conservative for 115200 baud (one FC 0x17 exchange takes ~4 ms
   //! median, ~5 ms p99). Rates are folded into [0.1, 1000] Hz: below the
   //! floor, procedures that wait on status stall for no reason; above
   //! the ceiling, no supported baud rate can carry the requests. A
   //! negative rate, or NaN, is a caller bug: the Gripper constructor
   //! throws DriverException for it.
   double connectionFrequency = 100.0; // Hz
};
} // namespace Robotiq
