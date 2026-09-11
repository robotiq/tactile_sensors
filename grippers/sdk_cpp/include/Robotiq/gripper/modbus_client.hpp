// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <cstdint>
#include <memory>

#include <Robotiq/detail/config.hpp>

namespace Robotiq {

class Logger;
class Serial;
struct ConnectionConfig;
struct GripperCommand;
struct GripperStatus;

//! \ingroup modbus_client
//! \brief Advanced, low-level access: one Modbus transaction per call.
//!
//! \warning **Use Gripper instead.** This class is supported public API,
//! but only because a few integrations genuinely cannot use Gripper —
//! reach for it only when one of these describes you:
//!   - a single-threaded target (a microcontroller superloop) that
//!     schedules the exchange itself and has no Platform to run a thread
//!     on;
//!   - setup or diagnostic tooling that must issue one specific
//!     transaction and see it fail;
//!   - a deterministic test bench that drives the bus step by step.
//!
//! Everything else — every application that controls a gripper — should
//! use Gripper, whose setCommand()/getStatus() read and write a process
//! image and never touch the bus. Driving this class from an application
//! means every accessor becomes a blocking Modbus transaction on the
//! caller's thread: the anti-pattern Gripper exists to prevent.
//!
//! Calls are scoped to the two blocks documented in the gripper's
//! instruction manual. The client owns the serial connection; it opens on
//! construction and closes on destruction. Not thread-safe: one client,
//! one thread.
class GripperModbusClient
{
public:
#if GRIPPERS_BUILD_DEFAULT_SERIAL
   //! \brief Open the built-in serial transport described by \p config.
   //! \param config see ConnectionConfig.
   //! \param logger Log sink; pass null to use the default stderr logger.
   //! \throw SerialIOException when the port cannot be opened/configured.
   explicit GripperModbusClient(const ConnectionConfig& config, std::shared_ptr<Logger> logger = nullptr);
#endif

   //! \brief Drive a caller-supplied transport; opens it if needed.
   //! \param serial The byte-level link to the gripper.
   //! \param slaveAddress The gripper's Modbus slave address.
   //! \param logger Log sink; pass null to use the default logger.
   GripperModbusClient(std::unique_ptr<Serial> serial, uint8_t slaveAddress, std::shared_ptr<Logger> logger = nullptr);

   //! Closes the serial connection.
   ~GripperModbusClient();

   GripperModbusClient(const GripperModbusClient&) = delete;
   GripperModbusClient& operator=(const GripperModbusClient&) = delete;

   //! \brief Read the status block (FC 0x03).
   //! \return The status block the gripper answered with.
   //! \throw DriverException on any transaction failure — Modbus protocol
   //!        errors (timeout, CRC, exception response) and wire-level
   //!        failures alike.
   [[nodiscard]] GripperStatus readStatus();

   //! \brief Write the command block (FC 0x10).
   //! \param command The block to write.
   //! \throw DriverException as for readStatus().
   void writeCommand(const GripperCommand& command);

   //! \brief Write the command block and read the status block in a single
   //! FC 0x17 transaction — the exchange step of a communication cycle.
   //! \param command The block to write.
   //! \return The status block the gripper answered with.
   //! \throw DriverException as for readStatus(); note that the exception
   //!        can happen after the write has been applied.
   [[nodiscard]] GripperStatus exchange(const GripperCommand& command);

private:
   struct Impl; // hides the nanomodbus client
   std::unique_ptr<Impl> _impl;
};
} // namespace Robotiq
