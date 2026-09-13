// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// The POSIX half of SerialPort: termios for the link parameters, a
// non-blocking descriptor plus poll() for the timeouts. Linux and macOS
// share it; the few places they differ are marked.

#include "detail/serial_port.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <string>
#include <system_error>

#include <Robotiq/gripper/serial_io_exception.hpp>

namespace Robotiq::detail {
namespace {
using std::chrono::milliseconds;
using std::chrono::steady_clock;

// The OS message for \p error, as std::system_error would render it.
// strerror() is not thread-safe and its _r variants disagree across
// platforms; the standard facility sidesteps both.
std::string describe(int error)
{
   return std::generic_category().message(error);
}

[[noreturn]] void fail(const std::string& context, int error)
{
   throw SerialIOException(context + ": " + describe(error));
}

// The termios constant for \p baudrate, or 0 when the platform has none.
// Only the rates the constants cover are offered: the raw-speed escapes
// (Linux BOTHER, macOS IOSSIOSPEED) are per-platform ioctls, and no
// Robotiq gripper runs outside this table.
speed_t baudConstant(uint32_t baudrate)
{
   switch(baudrate)
   {
   case 1200:
      return B1200;
   case 2400:
      return B2400;
   case 4800:
      return B4800;
   case 9600:
      return B9600;
   case 19200:
      return B19200;
   case 38400:
      return B38400;
   case 57600:
      return B57600;
   case 115200:
      return B115200;
   case 230400:
      return B230400;
#ifdef B460800
   case 460800:
      return B460800;
#endif
#ifdef B500000
   case 500000:
      return B500000;
#endif
#ifdef B921600
   case 921600:
      return B921600;
#endif
#ifdef B1000000
   case 1000000:
      return B1000000;
#endif
   default:
      return 0;
   }
}

// The wire format of the gripper's Modbus RTU link, on a raw terminal.
void configure(int fd, uint32_t baudrate)
{
   const speed_t speed = baudConstant(baudrate);
   if(speed == 0)
   {
      throw SerialIOException("unsupported baud rate " + std::to_string(baudrate));
   }

   struct termios tty = {};
   if(::tcgetattr(fd, &tty) != 0)
   {
      fail("reading the serial port attributes", errno);
   }

   // 8N1 without hardware flow control; CLOCAL keeps the port from
   // waiting on modem lines an RS-485 adapter does not drive.
   tty.c_cflag &= ~static_cast<tcflag_t>(PARENB | CSTOPB | CSIZE);
#ifdef CRTSCTS
   tty.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
#endif
   tty.c_cflag |= static_cast<tcflag_t>(CS8 | CREAD | CLOCAL);
   // No line editing, echo, signal characters, software flow control or
   // byte translation: any of them would alter a binary Modbus frame.
   tty.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ECHOE | ECHONL | ISIG | IEXTEN);
   tty.c_iflag &=
      ~static_cast<tcflag_t>(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
   tty.c_oflag &= ~static_cast<tcflag_t>(OPOST | ONLCR);
   // read() returns at once with whatever is buffered. The non-blocking
   // descriptor plus poll() in read() do the waiting, the only way to
   // express the zero-timeout drain the Serial contract requires.
   tty.c_cc[VMIN] = 0;
   tty.c_cc[VTIME] = 0;

   if(::cfsetispeed(&tty, speed) != 0 || ::cfsetospeed(&tty, speed) != 0)
   {
      fail("setting the baud rate", errno);
   }
   if(::tcsetattr(fd, TCSANOW, &tty) != 0)
   {
      fail("configuring the serial port", errno);
   }
}

// Both are conveniences rather than requirements of the link, and a
// pseudo-terminal supports neither, so failure is not an error:
// TIOCEXCL keeps a second process off the gripper, and clearing
// RTS/DTR matches what the port carried before this rewrite.
void applyLineDefaults(int fd)
{
#ifdef TIOCEXCL
   (void)::ioctl(fd, TIOCEXCL);
#endif
#ifdef TIOCMBIC
   int lines = TIOCM_RTS | TIOCM_DTR;
   (void)::ioctl(fd, TIOCMBIC, &lines);
#endif
}

// Wait for \p fd to become readable/writable, or for \p remaining to
// elapse. Returns false when the wait ran out or the device hung up.
bool waitReady(int fd, short events, std::chrono::nanoseconds remaining, const char* context)
{
   // Round up: a sub-millisecond remainder must not collapse to a
   // zero-length poll, which would spin until the deadline.
   const auto ms = std::chrono::ceil<milliseconds>(remaining).count();
   struct pollfd descriptor = {};
   descriptor.fd = fd;
   descriptor.events = events;

   const int ready = ::poll(&descriptor, 1, static_cast<int>(std::min<int64_t>(ms, INT32_MAX)));
   if(ready < 0)
   {
      if(errno == EINTR)
      {
         return true; // the caller re-checks its own deadline
      }
      fail(context, errno);
   }
   if((descriptor.revents & (POLLERR | POLLNVAL)) != 0)
   {
      fail(context, EIO);
   }
   // A hangup with nothing left to collect ends the transfer here; the
   // caller reports the short count, and the next call raises the error.
   return (descriptor.revents & events) != 0;
}
} // namespace

SerialPort::~SerialPort()
{
   SerialPort::close();
}

void SerialPort::open(const std::string& port, uint32_t baudrate)
{
   // O_NONBLOCK also keeps the open itself from hanging on a modem line
   // waiting for carrier; the timing behaviour it gives read()/write()
   // is what the timeouts below are built on.
   const int fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
   if(fd < 0)
   {
      const int error = errno;
      if(error == ENOENT || error == ENODEV || error == ENXIO)
      {
         throw SerialIOException("no serial port named '" + port + "' (is the device connected?)");
      }
      fail("opening serial port '" + port + "'", error);
   }

   try
   {
      configure(fd, baudrate);
   }
   catch(...)
   {
      ::close(fd);
      throw;
   }
   applyLineDefaults(fd);
   _fd = fd;
}

void SerialPort::close()
{
   if(_fd >= 0)
   {
#ifdef TIOCNXCL
      // Hand back the exclusive claim explicitly. Closing the descriptor
      // releases it only once the terminal itself goes away, which for a
      // pseudo-terminal is when its peer closes — so without this, a
      // reconnect on the same port would be refused as busy.
      (void)::ioctl(_fd, TIOCNXCL);
#endif
      ::close(_fd);
      _fd = -1;
   }
}

size_t SerialPort::read(uint8_t* data, size_t size, milliseconds timeout)
{
   const auto deadline = steady_clock::now() + timeout;
   size_t received = 0;
   while(received < size)
   {
      const ssize_t count = ::read(_fd, data + received, size - received);
      if(count > 0)
      {
         received += static_cast<size_t>(count);
         continue;
      }
      if(count < 0 && errno == EINTR)
      {
         continue;
      }
      if(count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
      {
         fail("reading from the serial port", errno);
      }

      // Nothing buffered — which a terminal reports as a zero-length
      // read, not EAGAIN, because VMIN and VTIME are both 0. A zero
      // timeout asks for exactly that much and stops here.
      const auto remaining = deadline - steady_clock::now();
      if(remaining <= std::chrono::nanoseconds::zero()
         || !waitReady(_fd, POLLIN, remaining, "waiting for serial input"))
      {
         break;
      }
   }
   return received;
}

size_t SerialPort::write(const uint8_t* data, size_t size, milliseconds timeout)
{
   const auto deadline = steady_clock::now() + timeout;
   size_t sent = 0;
   while(sent < size)
   {
      const ssize_t count = ::write(_fd, data + sent, size - sent);
      if(count > 0)
      {
         sent += static_cast<size_t>(count);
         continue;
      }
      if(count < 0 && errno == EINTR)
      {
         continue;
      }
      if(count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
      {
         fail("writing to the serial port", errno);
      }

      const auto remaining = deadline - steady_clock::now();
      if(remaining <= std::chrono::nanoseconds::zero()
         || !waitReady(_fd, POLLOUT, remaining, "waiting to send on the serial port"))
      {
         break;
      }
   }
   return sent;
}

void SerialPort::drain()
{
   while(::tcdrain(_fd) != 0)
   {
      if(errno != EINTR)
      {
         fail("draining the serial port", errno);
      }
   }
}
} // namespace Robotiq::detail
