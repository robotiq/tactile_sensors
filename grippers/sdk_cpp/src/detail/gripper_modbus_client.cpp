// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <nanomodbus.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/driver_exception.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/serial_io_exception.hpp>
#include <Robotiq/detail/byte_packing.hpp>
#include <Robotiq/detail/gripper_modbus_client.hpp>
#include <Robotiq/detail/modbus_constants.hpp>
#include <Robotiq/gripper/serial.hpp>

namespace Robotiq::detail {

namespace {

struct CallbackContext
{
   Serial* serial = nullptr;
   Logger* logger = nullptr;
};

// The FTDI latency timer is 16 ms by default on many platforms.
// The inter-byte timeout, set to 50 ms to avoid spurious timeout.
inline constexpr int32_t kByteTimeoutMs = 50;

// nanomodbus platform callbacks. Exceptions from the Serial layer must not
// cross into the C library, so they are converted to the nanomodbus
// convention: bytes-transferred on success, 0 on timeout, < 0 on error.
int32_t readSerial(uint8_t* buf, uint16_t count, int32_t byteTimeoutMs, void* arg)
{
   // nanomodbus reserves a negative timeout for "wait forever", which the
   // Serial contract cannot express (0 means drain without blocking).
   // Unreachable: the constructor always configures finite timeouts.
   assert(byteTimeoutMs >= 0);
   auto* context = static_cast<CallbackContext*>(arg);
   try
   {
      const auto timeout = std::chrono::milliseconds(byteTimeoutMs);
      const std::vector<uint8_t> data = context->serial->read(count, timeout);
      // The Serial contract is "up to count bytes", but this buffer belongs
      // to nanomodbus and the caller may supply their own Serial: take only
      // what was asked for, and say so rather than truncating silently.
      const auto received = std::min<std::size_t>(data.size(), count);
      if(received != data.size())
      {
         context->logger->log(Logger::Level::Error,
                              "serial read returned " + std::to_string(data.size()) + " bytes for a request of "
                                 + std::to_string(count) + "; ignoring the excess");
      }
      std::copy_n(data.begin(), received, buf);
      return static_cast<int32_t>(received); // short read = nanomodbus timeout
   }
   catch(const std::exception& e)
   {
      context->logger->log(Logger::Level::Error, std::string("serial read failed: ") + e.what());
      return -1;
   }
   catch(...)
   {
      context->logger->log(Logger::Level::Error, "serial read failed with an unknown exception");
      return -1;
   }
}

int32_t writeSerial(const uint8_t* buf, uint16_t count, int32_t /*byteTimeoutMs*/, void* arg)
{
   auto* context = static_cast<CallbackContext*>(arg);
   try
   {
      context->serial->write(std::vector<uint8_t>(buf, buf + count));
      return count;
   }
   catch(const std::exception& e)
   {
      context->logger->log(Logger::Level::Error, std::string("serial write failed: ") + e.what());
      return -1;
   }
   catch(...)
   {
      context->logger->log(Logger::Level::Error, "serial write failed with an unknown exception");
      return -1;
   }
}

// Turn a non-NONE nanomodbus error into a DriverException.
void check(nmbs_error err, const std::string& context)
{
   if(err != NMBS_ERROR_NONE)
   {
      throw DriverException(context + " failed: " + nmbs_strerror(err));
   }
}

} // namespace

struct GripperModbusClient::Impl
{
   std::unique_ptr<Serial> serial;
   std::shared_ptr<Logger> logger;
   CallbackContext context;
   nmbs_t nmbs{};
};

GripperModbusClient::GripperModbusClient(std::unique_ptr<Serial> serial,
                                         uint8_t slaveAddress,
                                         std::shared_ptr<Logger> logger)
   : _impl(std::make_unique<Impl>())
{
   _impl->serial = std::move(serial);
   _impl->logger = logger ? std::move(logger) : makeDefaultLogger();
   _impl->context = CallbackContext{_impl->serial.get(), _impl->logger.get()};

   if(!_impl->serial->isOpen())
   {
      _impl->serial->open();
   }

   nmbs_platform_conf platformConf;
   nmbs_platform_conf_create(&platformConf);
   platformConf.transport = NMBS_TRANSPORT_RTU;
   platformConf.read = readSerial;
   platformConf.write = writeSerial;
   platformConf.arg = &_impl->context;

   check(nmbs_client_create(&_impl->nmbs, &platformConf), "nmbs_client_create");

   // The configured timeout is the budget for a whole transaction; frame
   // delimiting gets its own, much shorter one.
   nmbs_set_read_timeout(&_impl->nmbs, static_cast<int32_t>(_impl->serial->getTimeout().count()));
   nmbs_set_byte_timeout(&_impl->nmbs, kByteTimeoutMs);
   nmbs_set_destination_rtu_address(&_impl->nmbs, slaveAddress);
}

GripperModbusClient::~GripperModbusClient() = default;

GripperStatus GripperModbusClient::readStatus()
{
   std::array<uint16_t, modbus_constants::kStatusRegisterCount> registers{};
   check(nmbs_read_holding_registers(&_impl->nmbs,
                                     modbus_constants::kStatusAddress,
                                     modbus_constants::kStatusRegisterCount,
                                     registers.data()),
         "readStatus");
   GripperStatus status;
   bytesFromRegisters(registers, status.data());
   return status;
}

void GripperModbusClient::writeCommand(const GripperCommand& command)
{
   const auto registers = registersFromBytes<modbus_constants::kCommandRegisterCount>(command.data());
   check(nmbs_write_multiple_registers(&_impl->nmbs,
                                       modbus_constants::kCommandAddress,
                                       modbus_constants::kCommandRegisterCount,
                                       registers.data()),
         "writeCommand");
}

GripperStatus GripperModbusClient::exchange(const GripperCommand& command)
{
   const auto commandRegisters = registersFromBytes<modbus_constants::kCommandRegisterCount>(command.data());
   std::array<uint16_t, modbus_constants::kStatusRegisterCount> statusRegisters{};
   check(nmbs_read_write_registers(&_impl->nmbs,
                                   modbus_constants::kStatusAddress,
                                   modbus_constants::kStatusRegisterCount,
                                   statusRegisters.data(),
                                   modbus_constants::kCommandAddress,
                                   modbus_constants::kCommandRegisterCount,
                                   commandRegisters.data()),
         "exchange");
   GripperStatus status;
   bytesFromRegisters(statusRegisters, status.data());
   return status;
}
} // namespace Robotiq::detail
