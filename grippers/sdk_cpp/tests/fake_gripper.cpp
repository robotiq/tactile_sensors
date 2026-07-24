// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "fake_gripper.hpp"

#include <nanomodbus.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/register_map.hpp>
#include <Robotiq/detail/modbus_constants.hpp>

namespace Robotiq::test {

namespace {
namespace mc = detail::modbus_constants;
namespace rm = register_map;

// The fake serves one contiguous register span, address 0 through the
// end of the status block, matching the device's tolerant register map.
// A request is valid when its last register falls inside it.
bool isValidRange(uint16_t address, uint16_t quantity)
{
   const uint32_t last = static_cast<uint32_t>(address) + quantity - 1;
   return last < mc::kStatusAddress + FakeGripperModbusServer::kBlockRegisters;
}
} // namespace

FakeGripperModbusServer::FakeGripperModbusServer(uint8_t slaveAddress)
{
   nmbs_platform_conf platformConf;
   nmbs_platform_conf_create(&platformConf);
   platformConf.transport = NMBS_TRANSPORT_RTU;
   platformConf.read = &FakeGripperModbusServer::readFromClient;
   platformConf.write = &FakeGripperModbusServer::writeToClient;
   platformConf.arg = this;

   nmbs_callbacks callbacks;
   nmbs_callbacks_create(&callbacks);
   callbacks.read_holding_registers = &FakeGripperModbusServer::onReadHoldingRegisters;
   callbacks.write_multiple_registers = &FakeGripperModbusServer::onWriteMultipleRegisters;
   callbacks.arg = this;

   const nmbs_error err = nmbs_server_create(&server, slaveAddress, &platformConf, &callbacks);
   if(err != NMBS_ERROR_NONE)
   {
      throw std::runtime_error("nmbs_server_create failed");
   }
   nmbs_set_read_timeout(&server, 10);
   nmbs_set_byte_timeout(&server, 10);
}

void FakeGripperModbusServer::poll()
{
   nmbs_server_poll(&server);
}

void FakeGripperModbusServer::givenGripperIsActivated()
{
   registers[mc::kStatusAddress] = static_cast<uint16_t>(
      (rm::kActivationStatusMask | (rm::kActivationStateComplete << rm::kActivationStateShift)) << 8);
   previousActivateBit = true;
   activationDone = true;
}

void FakeGripperModbusServer::givenGripperFault(uint8_t faultCode)
{
   registers[mc::kStatusAddress + 1] =
      static_cast<uint16_t>((faultCode << 8) | (registers[mc::kStatusAddress + 1] & 0xFF));
}

int32_t FakeGripperModbusServer::readFromClient(uint8_t* buf, uint16_t count, int32_t /*timeout*/, void* arg)
{
   auto* self = static_cast<FakeGripperModbusServer*>(arg);
   const auto available = std::min<size_t>(count, self->fromClientStream.size());
   std::copy_n(self->fromClientStream.begin(), available, buf);
   self->fromClientStream.erase(self->fromClientStream.begin(),
                                self->fromClientStream.begin() + static_cast<long>(available));
   return static_cast<int32_t>(available);
}

int32_t FakeGripperModbusServer::writeToClient(const uint8_t* buf, uint16_t count, int32_t /*timeout*/, void* arg)
{
   auto* self = static_cast<FakeGripperModbusServer*>(arg);
   self->toClientStream.insert(self->toClientStream.end(), buf, buf + count);
   return count;
}

nmbs_error FakeGripperModbusServer::onReadHoldingRegisters(uint16_t address,
                                                           uint16_t quantity,
                                                           uint16_t* registersOut,
                                                           uint8_t /*unitId*/,
                                                           void* arg)
{
   auto* self = static_cast<FakeGripperModbusServer*>(arg);
   if(!isValidRange(address, quantity))
   {
      return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
   }
   // A real gripper answers command-block reads with zeros and no error
   // (bench-verified): written commands never read back.
   for(uint16_t i = 0; i < quantity; ++i)
   {
      const uint16_t reg = address + i;
      const bool inCommandBlock =
         reg >= mc::kCommandAddress && reg < mc::kCommandAddress + FakeGripperModbusServer::kBlockRegisters;
      registersOut[i] = inCommandBlock ? 0 : self->registers[reg];
   }
   self->statusReads.fetch_add(1);
   return NMBS_ERROR_NONE;
}

nmbs_error FakeGripperModbusServer::onWriteMultipleRegisters(uint16_t address,
                                                             uint16_t quantity,
                                                             const uint16_t* values,
                                                             uint8_t /*unitId*/,
                                                             void* arg)
{
   auto* self = static_cast<FakeGripperModbusServer*>(arg);
   if(!isValidRange(address, quantity))
   {
      return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
   }
   std::copy_n(values, quantity, self->registers.begin() + address);
   self->commandWrites.fetch_add(1);
   self->simulate();
   return NMBS_ERROR_NONE;
}

void FakeGripperModbusServer::simulate()
{
   if(forcedStatusByte)
   {
      registers[mc::kStatusAddress] = static_cast<uint16_t>(*forcedStatusByte << 8);
      return;
   }
   const ActionRequest action{static_cast<uint8_t>(registers[mc::kCommandAddress] >> 8)};
   const auto requestedPosition = static_cast<uint8_t>(registers[mc::kCommandAddress + 1] & 0xFF);
   // A (pre-seeded) gripper fault latches until rACT is cleared, like
   // the real device; the full recovery then reactivates (clear, set).
   auto fault = static_cast<uint8_t>(registers[mc::kStatusAddress + 1] >> 8);
   const bool activateBit = action.get(ActionRequestBit::Activate);
   if(activateBit && !previousActivateBit)
   {
      activationDone = true; // rising edge: the sweep completes instantly
   }
   if(!activateBit)
   {
      if(previousActivateBit)
      {
         resets.fetch_add(1);
      }
      activationDone = false;
      fault = 0;
   }
   previousActivateBit = activateBit;
   uint8_t status = 0;
   if(activateBit)
   {
      status |= rm::kActivationStatusMask;
      if(activationDone)
      {
         status |= static_cast<uint8_t>(rm::kActivationStateComplete << rm::kActivationStateShift);
         if(action.get(ActionRequestBit::GoTo))
         {
            status |= rm::kGoToEchoMask;
            status |= static_cast<uint8_t>(rm::kObjectAtRequestedPosition << rm::kObjectDetectionShift);
         }
      }
   }
   registers[mc::kStatusAddress] = static_cast<uint16_t>(status << 8);
   registers[mc::kStatusAddress + 1] = static_cast<uint16_t>((fault << 8) | requestedPosition); // gFLT | gPR echo
   registers[mc::kStatusAddress + 2] = static_cast<uint16_t>((requestedPosition << 8) | kSimulatedCurrent); // gPO | gCU
}

FakeGripperSerial::FakeGripperSerial(FakeGripperModbusServer& gripper)
   : _gripper(gripper)
{
}

void FakeGripperSerial::open()
{
   _open = true;
}

bool FakeGripperSerial::isOpen() const
{
   return _open;
}

void FakeGripperSerial::close()
{
   _open = false;
}

std::vector<uint8_t> FakeGripperSerial::read(size_t size, std::chrono::milliseconds timeout)
{
   if(timeout.count() == 0)
   {
      return {}; // drains see no stale bytes; replies only exist after a request
   }
   const auto count = std::min(size, _gripper.toClientStream.size());
   std::vector<uint8_t> data(_gripper.toClientStream.begin(),
                             _gripper.toClientStream.begin() + static_cast<long>(count));
   _gripper.toClientStream.erase(_gripper.toClientStream.begin(),
                                 _gripper.toClientStream.begin() + static_cast<long>(count));
   return data;
}

void FakeGripperSerial::write(const std::vector<uint8_t>& data)
{
   _gripper.fromClientStream.insert(_gripper.fromClientStream.end(), data.begin(), data.end());
   _gripper.poll();
}

std::chrono::milliseconds FakeGripperSerial::getTimeout() const
{
   return std::chrono::milliseconds{100};
}
} // namespace Robotiq::test
