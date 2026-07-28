// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Advanced, low-level access: one Modbus transaction per call.
//! ⚠ This is not the application API — use Gripper, whose accessors never
//! touch the bus. GripperModbusClient exists for the audiences that need
//! direct transactions: setup/diagnostic tooling, deterministic test
//! benches, and no-thread integrations (microcontroller-style superloops)
//! that schedule the exchange themselves.
//! Calls are scoped to the two blocks documented in the gripper's
//! instruction manual. The client owns the serial connection; it opens on
//! construction and closes on destruction. Not thread-safe: one client,
//! one thread.

#pragma once

#include <cstdint>
#include <memory>

namespace Robotiq {
class Logger;
struct ConnectionConfig;
struct GripperCommand;
struct GripperStatus;
} // namespace Robotiq

namespace Robotiq::detail {
class Serial;

class GripperModbusClient
{
public:
   // \param logger Log sink; pass null to use the default stderr logger.
   // \throw SerialIOException when the port cannot be opened/configured.
   explicit GripperModbusClient(const ConnectionConfig& config, std::shared_ptr<Logger> logger = nullptr);

   // Constructor for unit tests or custom serial implementations; opens
   // the port if needed.
   GripperModbusClient(std::unique_ptr<Serial> serial, uint8_t slaveAddress, std::shared_ptr<Logger> logger = nullptr);

   ~GripperModbusClient();

   GripperModbusClient(const GripperModbusClient&) = delete;
   GripperModbusClient& operator=(const GripperModbusClient&) = delete;

   // Read the status block (FC 0x03).
   // \throw DriverException on any transaction failure — Modbus protocol
   //        errors (timeout, CRC, exception response) and wire-level
   //        failures alike.
   [[nodiscard]] GripperStatus readStatus();

   // Write the command block (FC 0x10).
   // \throw DriverException as for readStatus().
   void writeCommand(const GripperCommand& command);

   // Write the command block and read the status block in a single
   // FC 0x17 transaction — the exchange step of a communication cycle.
   // \throw DriverException as for readStatus(); note that the exception
   // can happen after the write has been applied.
   [[nodiscard]] GripperStatus exchange(const GripperCommand& command);

private:
   struct Impl; // hides the nanomodbus client
   std::unique_ptr<Impl> _impl;
};
} // namespace Robotiq::detail
