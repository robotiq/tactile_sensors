// Copyright (c) 2023 PickNik, Inc.
// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace Robotiq {

//! \ingroup transport
//! \brief Byte-level transport to the gripper hardware — the transport
//! extension point.
//!
//! The driver talks to the hardware through an implementation of this
//! interface: the built-in libserialport transport on a desktop, an
//! application's own (a TCP-serial bridge, an MCU UART/DMA transport),
//! or a scripted connection in tests. Link parameters (port, baud rate,
//! timeout, ...) are fixed at construction of the implementation — see
//! SerialConfig — so a connection cannot be silently reconfigured while
//! open.
//!
//! \warning On an RTOS, read() MUST yield the CPU while awaiting bytes
//! (e.g. interrupt/DMA completion signalled through a semaphore): it
//! runs on the exchange thread, and a busy-wait there starves
//! lower-priority tasks — see the caveats in
//! ports/threadx/threadx_platform.hpp.
//!
//! \see makeFakeGripper() for testing without hardware; ports/threadx/ for a
//! reference MCU transport.
class Serial
{
public:
   virtual ~Serial() = default;

   //! \brief Open the configured link.
   //! \throw SerialIOException when the port cannot be opened/configured.
   virtual void open() = 0;

   //! \return true if the link is currently open.
   [[nodiscard]] virtual bool isOpen() const = 0;

   //! Close the link. Safe to call when already closed.
   virtual void close() = 0;

   //! \brief Read up to \p size bytes from the serial port.
   //! \param size Maximum number of bytes to read.
   //! \param timeout How long to wait for bytes to arrive. A zero timeout
   //!        returns only what is instantly available (used to drain
   //!        stale bytes before a request).
   //! \return The bytes received — possibly fewer than \p size, or empty
   //!         when nothing arrived in time.
   //! \throw SerialIOException on wire-level failure.
   [[nodiscard]] virtual std::vector<uint8_t> read(size_t size, std::chrono::milliseconds timeout) = 0;

   //! \brief Write a sequence of bytes to the serial port.
   //! \param data The bytes to write.
   //! \throw SerialIOException on timeout or wire-level failure.
   virtual void write(const std::vector<uint8_t>& data) = 0;

   //! \return The per-transaction read/write timeout of this connection.
   [[nodiscard]] virtual std::chrono::milliseconds getTimeout() const = 0;
};
} // namespace Robotiq
