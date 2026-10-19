// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// The Windows half of SerialPort: a comm handle configured through a DCB,
// with COMMTIMEOUTS doing the waiting that poll() does on POSIX.

#include "detail/serial_port.hpp"

#define WIN32_LEAN_AND_MEAN
// Recent toolchains predefine NOMINMAX; redefining it is a warning, fatal
// under warnings-as-errors.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10
#endif
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <system_error>

#include <Robotiq/gripper/serial_io_exception.hpp>

namespace Robotiq::detail {
namespace {
using std::chrono::milliseconds;

[[noreturn]] void fail(const std::string& context, DWORD error)
{
   throw SerialIOException(context + ": " + std::system_category().message(static_cast<int>(error)));
}

// "COM10" and above only resolve through the device namespace, and the
// prefix is harmless on COM1-9, so a bare COM name gets it. Every other
// spelling is handed to the OS exactly as the caller wrote it: an
// already-prefixed \\.\ name, a \\?\ device path, and equally a name that
// is nothing of the kind, which then fails as the plain open it is
// rather than as a malformed device path.
std::string devicePath(const std::string& port)
{
   const bool isBareComName = port.size() > 3 && ::_strnicmp(port.c_str(), "COM", 3) == 0
                           && port.find_first_not_of("0123456789", 3) == std::string::npos;
   return isBareComName ? "\\\\.\\" + port : port;
}

// 8N1, no flow control, no line handling: the wire format of the
// gripper's Modbus RTU link. RTS and DTR are left deasserted.
void configure(HANDLE handle, uint32_t baudrate)
{
   DCB dcb = {};
   dcb.DCBlength = sizeof(dcb);
   if(!::GetCommState(handle, &dcb))
   {
      fail("reading the serial port state", ::GetLastError());
   }

   dcb.BaudRate = baudrate;
   dcb.ByteSize = 8;
   dcb.Parity = NOPARITY;
   dcb.StopBits = ONESTOPBIT;
   dcb.fBinary = TRUE;
   dcb.fParity = FALSE;
   dcb.fOutxCtsFlow = FALSE;
   dcb.fOutxDsrFlow = FALSE;
   dcb.fDtrControl = DTR_CONTROL_DISABLE;
   dcb.fDsrSensitivity = FALSE;
   dcb.fTXContinueOnXoff = TRUE;
   dcb.fOutX = FALSE;
   dcb.fInX = FALSE;
   dcb.fErrorChar = FALSE;
   dcb.fNull = FALSE;
   dcb.fRtsControl = RTS_CONTROL_DISABLE;
   dcb.fAbortOnError = FALSE;

   if(!::SetCommState(handle, &dcb))
   {
      // An unsupported baud rate surfaces here, as ERROR_INVALID_PARAMETER.
      fail("configuring the serial port at " + std::to_string(baudrate) + " baud", ::GetLastError());
   }
}
} // namespace

SerialPort::~SerialPort()
{
   SerialPort::close();
}

void SerialPort::open(const std::string& port, uint32_t baudrate)
{
   // No sharing: a second opener of the same gripper is a bug, and the
   // driver enforcing it beats two processes interleaving frames.
   const HANDLE handle =
      ::CreateFileA(devicePath(port).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
   if(handle == INVALID_HANDLE_VALUE)
   {
      const DWORD error = ::GetLastError();
      if(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
      {
         throw SerialIOException("no serial port named '" + port + "' (is the device connected?)");
      }
      fail("opening serial port '" + port + "'", error);
   }

   _handle = handle;
   try
   {
      configure(_handle, baudrate);
      // Establishes the cached pair, so later reads and writes only
      // re-enter the driver when the timeout actually changes.
      setTimeouts(0, 0);
   }
   catch(...)
   {
      close();
      throw;
   }
}

void SerialPort::close()
{
   if(_handle != nullptr)
   {
      ::CloseHandle(_handle);
      _handle = nullptr;
      _readTimeoutMs = -1;
      _writeTimeoutMs = -1;
   }
}

void SerialPort::setTimeouts(int64_t readMs, int64_t writeMs)
{
   if(readMs == _readTimeoutMs && writeMs == _writeTimeoutMs)
   {
      return;
   }

   COMMTIMEOUTS timeouts = {};
   if(readMs == 0)
   {
      // Documented as "return immediately with whatever is buffered" —
      // the zero-timeout drain the Serial contract requires.
      timeouts.ReadIntervalTimeout = MAXDWORD;
   }
   else
   {
      // ReadFile returns once the full count arrives or this elapses.
      timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(readMs);
   }
   timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(writeMs);

   if(!::SetCommTimeouts(_handle, &timeouts))
   {
      fail("setting the serial port timeouts", ::GetLastError());
   }
   _readTimeoutMs = readMs;
   _writeTimeoutMs = writeMs;
}

size_t SerialPort::read(uint8_t* data, size_t size, milliseconds timeout)
{
   if(size == 0)
   {
      return 0;
   }
   setTimeouts(timeout.count(), _writeTimeoutMs);

   DWORD received = 0;
   if(!::ReadFile(_handle, data, static_cast<DWORD>(size), &received, nullptr))
   {
      fail("reading from the serial port", ::GetLastError());
   }
   return received;
}

size_t SerialPort::write(const uint8_t* data, size_t size, milliseconds timeout)
{
   if(size == 0)
   {
      return 0;
   }
   setTimeouts(_readTimeoutMs, timeout.count());

   DWORD sent = 0;
   if(!::WriteFile(_handle, data, static_cast<DWORD>(size), &sent, nullptr))
   {
      fail("writing to the serial port", ::GetLastError());
   }
   return sent;
}

bool SerialPort::drain(milliseconds /*timeout*/)
{
   if(!::FlushFileBuffers(_handle))
   {
      fail("draining the serial port", ::GetLastError());
   }
   return true;
}
} // namespace Robotiq::detail
