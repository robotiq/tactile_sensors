// Copyright (c) 2023 PickNik, Inc.
// Copyright (c) 2026 Robotiq, Inc. (libserialport rewrite)
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/detail/default_serial.hpp>

#include <libserialport.h>

#include <fstream>
#include <string>
#include <utility>

#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/serial_io_exception.hpp>

namespace Robotiq::detail {
namespace {
//! Render a libserialport return code as a message and free any error state.
std::string describeSpError(sp_return result)
{
   if(result == SP_ERR_FAIL)
   {
      char* message = sp_last_error_message();
      std::string text = message != nullptr ? message : "unknown OS error";
      sp_free_error_message(message);
      return text;
   }
   switch(result)
   {
   case SP_ERR_ARG:
      return "invalid argument";
   case SP_ERR_MEM:
      return "memory allocation failure";
   case SP_ERR_SUPP:
      return "operation not supported";
   default:
      return "error " + std::to_string(result);
   }
}

//! Throw SerialIOException when a libserialport call fails.
void check(sp_return result, const std::string& context)
{
   if(result < SP_OK)
   {
      throw SerialIOException(context + ": " + describeSpError(result));
   }
}

//! Strip "/dev/" prefix and return just the device name (e.g. "ttyUSB0").
std::string deviceBasename(const std::string& port)
{
   const auto slash = port.find_last_of('/');
   return slash == std::string::npos ? port : port.substr(slash + 1);
}
} // namespace

DefaultSerial::DefaultSerial(SerialConfig config, std::shared_ptr<Logger> logger)
   : _config(std::move(config))
   , _logger(logger ? std::move(logger) : makeDefaultLogger())
{
}

DefaultSerial::~DefaultSerial()
{
   DefaultSerial::close();
}

void DefaultSerial::open()
{
   if(_portHandle != nullptr)
   {
      _logger->logf(Logger::Level::Debug,
                    "open() called on %s but the port is already open; ignoring.",
                    _config.port.c_str());
      return;
   }
   if(_config.port.empty())
   {
      throw SerialIOException("Cannot open serial port: port path is empty");
   }

   check(sp_get_port_by_name(_config.port.c_str(), &_portHandle), "sp_get_port_by_name(" + _config.port + ")");
   try
   {
      check(sp_open(_portHandle, SP_MODE_READ_WRITE), "sp_open(" + _config.port + ")");
      check(sp_set_baudrate(_portHandle, static_cast<int>(_config.baudrate)), "sp_set_baudrate");
      check(sp_set_bits(_portHandle, 8), "sp_set_bits");
      check(sp_set_parity(_portHandle, SP_PARITY_NONE), "sp_set_parity");
      check(sp_set_stopbits(_portHandle, 1), "sp_set_stopbits");
      check(sp_set_flowcontrol(_portHandle, SP_FLOWCONTROL_NONE), "sp_set_flowcontrol");
   }
   catch(...)
   {
      sp_free_port(_portHandle);
      _portHandle = nullptr;
      throw;
   }

#ifdef __linux__
   if(_config.latencyTimerMs > 0)
   {
      if(applyLatencyTimer())
      {
         _logger->logf(Logger::Level::Info,
                       "FTDI latency_timer set to %d ms on %s.",
                       _config.latencyTimerMs,
                       _config.port.c_str());
      }
      else
      {
         _logger->logf(Logger::Level::Warn,
                       "Could not set FTDI latency_timer to %d ms on %s. Modbus cycle latency may be "
                       "~3x higher than expected (the kernel default is 16 ms). Either run with "
                       "permissions to write /sys/bus/usb-serial/devices/<dev>/latency_timer, or ship "
                       "a udev rule that sets it at plug time.",
                       _config.latencyTimerMs,
                       _config.port.c_str());
      }
   }
#endif
}

bool DefaultSerial::isOpen() const
{
   return _portHandle != nullptr;
}

void DefaultSerial::close()
{
   if(_portHandle != nullptr)
   {
      sp_close(_portHandle);
      sp_free_port(_portHandle);
      _portHandle = nullptr;
   }
}

std::vector<uint8_t> DefaultSerial::read(size_t size, std::chrono::milliseconds timeout)
{
   if(_portHandle == nullptr)
   {
      throw SerialIOException("read called on closed port");
   }

   std::vector<uint8_t> data(size);
   const sp_return result =
      timeout.count() == 0
         ? sp_nonblocking_read(_portHandle, data.data(), size)
         : sp_blocking_read(_portHandle, data.data(), size, static_cast<unsigned int>(timeout.count()));
   check(result, "sp_read");
   data.resize(static_cast<size_t>(result));
   return data;
}

void DefaultSerial::write(const std::vector<uint8_t>& data)
{
   if(_portHandle == nullptr)
   {
      throw SerialIOException("write called on closed port");
   }

   const sp_return result =
      sp_blocking_write(_portHandle, data.data(), data.size(), static_cast<unsigned int>(_config.timeout.count()));
   check(result, "sp_blocking_write");
   if(static_cast<size_t>(result) < data.size())
   {
      throw SerialIOException("write timeout: wrote " + std::to_string(result) + " of " + std::to_string(data.size())
                              + " bytes");
   }
   check(sp_drain(_portHandle), "sp_drain");
}

std::chrono::milliseconds DefaultSerial::getTimeout() const
{
   return _config.timeout;
}

const SerialConfig& DefaultSerial::getConfig() const
{
   return _config;
}

bool DefaultSerial::applyLatencyTimer() const
{
#ifdef __linux__
   const std::string base = deviceBasename(_config.port);
   if(base.empty())
   {
      return false;
   }
   const std::string path = "/sys/bus/usb-serial/devices/" + base + "/latency_timer";

   // Read-before-write: sysfs is root-owned, so an unprivileged caller can't
   // write to it. If a privileged init (udev rule, container entrypoint) has
   // already pinned the value, skip the write — otherwise we'd emit a
   // spurious WARN about a device that's already configured.
   {
      std::ifstream in(path);
      int current = -1;
      if(in >> current && current == _config.latencyTimerMs)
      {
         return true;
      }
   }

   std::ofstream out(path);
   if(!out.is_open())
   {
      return false;
   }
   out << _config.latencyTimerMs;
   return out.good();
#else
   // No sysfs on this platform; the FTDI latency timer is a driver setting
   // (e.g. Windows device manager). Nothing to enforce programmatically.
   return false;
#endif
}
} // namespace Robotiq::detail
