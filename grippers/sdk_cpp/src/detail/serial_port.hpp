// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The OS serial handle behind DefaultSerial — termios on
//!        Linux/macOS, the Win32 comm API on Windows.
//! One translation unit per platform family implements it (posix/,
//! win32/); the header is internal to the SDK, since the transport seam
//! consumers extend is Robotiq::Serial.
//! Every operation reports failure as SerialIOException. The read
//! contract is the one Robotiq::Serial documents: a zero timeout takes
//! only what is already buffered, a positive one waits for \p size bytes
//! or the deadline, whichever comes first.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Robotiq::detail {
class SerialPort
{
public:
   SerialPort() = default;
   ~SerialPort();

   SerialPort(const SerialPort&) = delete;
   SerialPort& operator=(const SerialPort&) = delete;

   // Open \p port at \p baudrate, 8N1, no flow control, raw.
   void open(const std::string& port, uint32_t baudrate);

   // Release the handle. Safe to call when already closed.
   void close();

   // Bytes actually read — fewer than size when the deadline passed first.
   [[nodiscard]] size_t read(uint8_t* data, size_t size, std::chrono::milliseconds timeout);

   // Bytes actually written; a short count means the write timed out.
   [[nodiscard]] size_t write(const uint8_t* data, size_t size, std::chrono::milliseconds timeout);

   // Block until the bytes written have left the transmit buffer.
   void drain();

private:
#ifdef _WIN32
   // The COMMTIMEOUTS the handle currently carries, so a steady exchange
   // does not re-enter the driver on every read; -1 until open() applies
   // the first pair. Both fields live in one struct, hence one cache.
   void setTimeouts(int64_t readMs, int64_t writeMs);

   void* _handle = nullptr; // HANDLE, kept untyped to spare callers <windows.h>
   int64_t _readTimeoutMs = -1;
   int64_t _writeTimeoutMs = -1;
#else
   int _fd = -1;
#endif
};
} // namespace Robotiq::detail
