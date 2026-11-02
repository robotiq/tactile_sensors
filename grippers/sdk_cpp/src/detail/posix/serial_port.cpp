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
#include <thread>

#include <Robotiq/gripper/serial_io_exception.hpp>

namespace Robotiq::detail {
namespace {
using std::chrono::milliseconds;
using std::chrono::steady_clock;

// 8N1: a start bit, 8 data bits and a stop bit.
constexpr uint32_t kWireBitsPerByte = 10;

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

   // No line editing, echo, signal characters, software flow control or
   // byte translation: any of them would alter a binary Modbus frame.
   // glibc's cfmakeraw() leaves the input flags below set.
   ::cfmakeraw(&tty);
   tty.c_iflag &= ~static_cast<tcflag_t>(IXOFF | IXANY | INPCK);
#ifdef IUCLC
   tty.c_iflag &= ~static_cast<tcflag_t>(IUCLC);
#endif
   // 8N1 without hardware flow control; CLOCAL keeps the port from
   // waiting on modem lines an RS-485 adapter does not drive.
   tty.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);
#ifdef CRTSCTS
   tty.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
#endif
   tty.c_cflag |= static_cast<tcflag_t>(CREAD | CLOCAL);
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

[[noreturn]] void failHungUp(const std::string& context)
{
   throw SerialIOException(context + ": the other end hung up");
}

enum class WaitResult
{
   Ready,
   Interrupted,
   TimedOut,
};

// Wait for \p fd to become readable/writable, or for \p remaining to
// elapse.
WaitResult waitReady(int fd, short events, std::chrono::nanoseconds remaining, const char* context)
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
         return WaitResult::Interrupted;
      }
      fail(context, errno);
   }
   if((descriptor.revents & (POLLERR | POLLNVAL)) != 0)
   {
      fail(context, EIO);
   }
   // Bytes still buffered behind a hangup are collected first; the
   // caller raises once a read finds nothing left.
   if((descriptor.revents & events) == 0 && (descriptor.revents & POLLHUP) != 0)
   {
      failHungUp(context);
   }
   return ready == 0 ? WaitResult::TimedOut : WaitResult::Ready;
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
   _baudrate = baudrate;
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
   bool polledReadable = false;
   while(received < size)
   {
      const ssize_t count = ::read(_fd, data + received, size - received);
      if(count > 0)
      {
         received += static_cast<size_t>(count);
         continue;
      }
      if(count == 0 && polledReadable)
      {
         failHungUp("reading from the serial port");
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
      if(remaining <= std::chrono::nanoseconds::zero())
      {
         break;
      }
      const WaitResult result = waitReady(_fd, POLLIN, remaining, "waiting for serial input");
      if(result == WaitResult::TimedOut)
      {
         break;
      }
      polledReadable = result == WaitResult::Ready;
   }
   return received;
}

size_t SerialPort::write(const uint8_t* data, size_t size, milliseconds timeout)
{
   const bool unbounded = timeout == milliseconds::zero();
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

      const auto remaining = unbounded ? std::chrono::nanoseconds::max() : deadline - steady_clock::now();
      if(remaining <= std::chrono::nanoseconds::zero()
         || waitReady(_fd, POLLOUT, remaining, "waiting to send on the serial port") == WaitResult::TimedOut)
      {
         break;
      }
   }
   return sent;
}

bool SerialPort::drain(milliseconds timeout)
{
   // tcdrain() alone has no deadline, and a pseudo-terminal drains only
   // as fast as its peer reads, so a stalled socat would hang it. The
   // output queue is watched against the deadline first; what tcdrain()
   // still waits for after that is the adapter's own FIFO.
#ifdef TIOCOUTQ
   const bool unbounded = timeout == milliseconds::zero();
   const auto deadline = steady_clock::now() + timeout;
   const auto byteTime = std::chrono::microseconds(std::chrono::seconds(1)) * kWireBitsPerByte / _baudrate;
   int queued = 0;
   while(::ioctl(_fd, TIOCOUTQ, &queued) == 0 && queued > 0)
   {
      if(!unbounded && steady_clock::now() >= deadline)
      {
         return false;
      }
      std::this_thread::sleep_for(byteTime * queued);
   }
#else
   (void)timeout;
#endif
   while(::tcdrain(_fd) != 0)
   {
      if(errno != EINTR)
      {
         fail("draining the serial port", errno);
      }
   }
   return true;
}
} // namespace Robotiq::detail
