// Copyright (c) 2023 PickNik, Inc.
// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Byte-level transport to the gripper hardware — the transport
//! extension point. The driver talks to the hardware through an
//! implementation of this interface: the built-in libserialport transport
//! on a desktop, an application's own (a TCP-serial bridge, an MCU
//! UART/DMA transport), or a scripted connection in tests.
//! Link parameters (port, baud rate, timeout, ...) are fixed at
//! construction of the implementation — see SerialConfig — so a connection
//! cannot be silently reconfigured while open.
//! ⚠ On an RTOS, read() MUST yield the CPU while awaiting bytes (e.g.
//! interrupt/DMA completion signalled through a semaphore): it runs on the
//! exchange thread, and a busy-wait there starves lower-priority tasks —
//! see the caveats in ports/threadx/threadx_platform.hpp.

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace Robotiq {
class Serial
{
public:
   virtual ~Serial() = default;

   // \throw SerialIOException when the port cannot be opened/configured.
   virtual void open() = 0;

   [[nodiscard]] virtual bool isOpen() const = 0;

   virtual void close() = 0;

   // Read up to \p size bytes from the serial port, waiting at most
   // \p timeout. A zero timeout returns only what is instantly
   // available (used to drain stale bytes before a request).
   // \return The bytes received — possibly fewer than \p size, or
   //         empty when nothing arrived in time.
   // \throw SerialIOException on wire-level failure.
   [[nodiscard]] virtual std::vector<uint8_t> read(size_t size, std::chrono::milliseconds timeout) = 0;

   // Write a sequence of bytes to the serial port.
   // \param data A vector containing data to be written to the serial port.
   // \throw SerialIOException on timeout or wire-level failure.
   virtual void write(const std::vector<uint8_t>& data) = 0;

   // Get the per-transaction timeout of this connection.
   // \return Read/write timeout in milliseconds.
   [[nodiscard]] virtual std::chrono::milliseconds getTimeout() const = 0;
};
} // namespace Robotiq
