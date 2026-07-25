// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "fake/gripper_server.hpp"

#include <algorithm>
#include <deque>
#include <string>

#include <nanomodbus.h>

#include <Robotiq/gripper/driver_exception.hpp>

#include "fake/register_model.hpp"

namespace Robotiq::fake {

//! The nanomodbus server and the byte queues it reads and writes. Kept out of
//! the header so the vendored C library stays confined to this file — the same
//! arrangement GripperModbusClient uses on the real path.
struct GripperServer::Impl
{
   explicit Impl(RegisterModel& registerModel)
      : model(registerModel)
   {
   }

   // nanomodbus platform and register callbacks. Members rather than free
   // functions so they can reach this type, which the header keeps private.
   static int32_t readFromClient(uint8_t* buf, uint16_t count, int32_t /*timeout*/, void* arg)
   {
      auto* impl = static_cast<Impl*>(arg);
      const auto available = std::min<size_t>(count, impl->fromClient.size());
      std::copy_n(impl->fromClient.begin(), available, buf);
      impl->fromClient.erase(impl->fromClient.begin(), impl->fromClient.begin() + static_cast<long>(available));
      return static_cast<int32_t>(available);
   }

   static int32_t writeToClient(const uint8_t* buf, uint16_t count, int32_t /*timeout*/, void* arg)
   {
      auto* impl = static_cast<Impl*>(arg);
      impl->toClient.insert(impl->toClient.end(), buf, buf + count);
      return count;
   }

   static nmbs_error onReadHoldingRegisters(uint16_t address,
                                            uint16_t quantity,
                                            uint16_t* registersOut,
                                            uint8_t /*unitId*/,
                                            void* arg)
   {
      auto* impl = static_cast<Impl*>(arg);
      if(!RegisterModel::containsRange(address, quantity))
      {
         return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
      }
      impl->model.read(address, quantity, registersOut);
      return NMBS_ERROR_NONE;
   }

   static nmbs_error onWriteMultipleRegisters(uint16_t address,
                                              uint16_t quantity,
                                              const uint16_t* values,
                                              uint8_t /*unitId*/,
                                              void* arg)
   {
      auto* impl = static_cast<Impl*>(arg);
      if(!RegisterModel::containsRange(address, quantity))
      {
         return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
      }
      impl->model.write(address, quantity, values);
      return NMBS_ERROR_NONE;
   }

   RegisterModel& model;
   std::deque<uint8_t> fromClient;
   std::deque<uint8_t> toClient;
   nmbs_t server{};
};

GripperServer::GripperServer(RegisterModel& model, uint8_t slaveAddress)
   : _impl(std::make_unique<Impl>(model))
{
   nmbs_platform_conf platformConf;
   nmbs_platform_conf_create(&platformConf);
   platformConf.transport = NMBS_TRANSPORT_RTU;
   platformConf.read = &Impl::readFromClient;
   platformConf.write = &Impl::writeToClient;
   platformConf.arg = _impl.get();

   nmbs_callbacks callbacks;
   nmbs_callbacks_create(&callbacks);
   callbacks.read_holding_registers = &Impl::onReadHoldingRegisters;
   callbacks.write_multiple_registers = &Impl::onWriteMultipleRegisters;
   callbacks.arg = _impl.get();

   const nmbs_error err = nmbs_server_create(&_impl->server, slaveAddress, &platformConf, &callbacks);
   if(err != NMBS_ERROR_NONE)
   {
      throw DriverException(std::string("creating the fake gripper's Modbus server failed: ") + nmbs_strerror(err));
   }
   nmbs_set_read_timeout(&_impl->server, 10);
   nmbs_set_byte_timeout(&_impl->server, 10);
}

GripperServer::~GripperServer() = default;

void GripperServer::deliver(const std::vector<uint8_t>& request)
{
   _impl->fromClient.insert(_impl->fromClient.end(), request.begin(), request.end());
   nmbs_server_poll(&_impl->server);
}

std::vector<uint8_t> GripperServer::drain(size_t max)
{
   auto& toClient = _impl->toClient;
   const auto count = std::min(max, toClient.size());
   std::vector<uint8_t> data(toClient.begin(), toClient.begin() + static_cast<long>(count));
   toClient.erase(toClient.begin(), toClient.begin() + static_cast<long>(count));
   return data;
}

void GripperServer::discardPendingReply()
{
   _impl->toClient.clear();
}

GripperSerial::GripperSerial(GripperServer& gripperServer)
   : _gripperServer(gripperServer)
{
}

void GripperSerial::open()
{
   _open = true;
}

bool GripperSerial::isOpen() const
{
   return _open;
}

void GripperSerial::close()
{
   _open = false;
}

std::vector<uint8_t> GripperSerial::read(size_t size, std::chrono::milliseconds timeout)
{
   if(timeout.count() == 0)
   {
      return {}; // drains see no stale bytes; replies only exist after a request
   }
   return _gripperServer.drain(size);
}

void GripperSerial::write(const std::vector<uint8_t>& data)
{
   _gripperServer.deliver(data);
}

std::chrono::milliseconds GripperSerial::getTimeout() const
{
   return std::chrono::milliseconds{100};
}
} // namespace Robotiq::fake
