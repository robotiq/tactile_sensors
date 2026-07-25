// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "fake/register_model.hpp"

#include <algorithm>

#include <Robotiq/detail/byte_packing.hpp>

#include "fake/status_writer.hpp"

namespace Robotiq::fake {
namespace {
namespace mc = detail::modbus_constants;
} // namespace

bool RegisterModel::containsRange(uint16_t address, uint16_t quantity)
{
   const uint32_t last = static_cast<uint32_t>(address) + quantity - 1;
   return last < kRegisterCount;
}

void RegisterModel::read(uint16_t address, uint16_t quantity, uint16_t* out) const
{
   for(uint16_t i = 0; i < quantity; ++i)
   {
      const uint16_t reg = address + i;
      const bool inCommandBlock = reg >= mc::kCommandAddress && reg < mc::kCommandAddress + kBlockRegisters;
      out[i] = inCommandBlock ? 0 : _registers[reg];
   }
}

void RegisterModel::write(uint16_t address, uint16_t quantity, const uint16_t* values)
{
   std::copy_n(values, quantity, _registers.begin() + address);
   setStatus(processCommand(command(), status()));
}

GripperCommand RegisterModel::command() const
{
   std::array<uint16_t, kBlockRegisters> block{};
   std::copy_n(_registers.begin() + mc::kCommandAddress, kBlockRegisters, block.begin());

   GripperCommand decoded;
   detail::bytesFromRegisters<kBlockRegisters>(block, decoded.data());
   return decoded;
}

GripperStatus RegisterModel::status() const
{
   std::array<uint16_t, kBlockRegisters> block{};
   std::copy_n(_registers.begin() + mc::kStatusAddress, kBlockRegisters, block.begin());

   GripperStatus decoded;
   detail::bytesFromRegisters<kBlockRegisters>(block, decoded.data());
   return decoded;
}

void RegisterModel::setStatus(const GripperStatus& status)
{
   const auto block = detail::registersFromBytes<kBlockRegisters>(status.data());
   std::copy(block.begin(), block.end(), _registers.begin() + mc::kStatusAddress);
}

GripperStatus RegisterModel::processCommand(const GripperCommand& command, const GripperStatus& currentStatus)
{
   const bool activateBit = command.action.get(ActionRequestBit::Activate);

   // A latched gripper fault survives until rACT is cleared, as on the real
   // device; the full recovery then reactivates (clear, set).
   uint8_t fault = currentStatus.faultStatus.raw();

   if(activateBit && !_previousActivateBit)
   {
      _activationDone = true; // rising edge: the sweep completes instantly
   }
   if(!activateBit)
   {
      _activationDone = false;
      fault = 0;
   }
   _previousActivateBit = activateBit;

   GripperStatus nextStatus;
   setFaultStatusByte(nextStatus, fault);
   setActivated(nextStatus, activateBit);
   if(activateBit && _activationDone)
   {
      setActivationState(nextStatus, ActivationState::Complete);
      if(command.action.get(ActionRequestBit::GoTo))
      {
         setGoToEnabled(nextStatus, true);
         setObjectDetection(nextStatus, ObjectDetection::AtRequestedPosition);
      }
   }
   nextStatus.positionRequestEcho = command.positionRequest;
   nextStatus.position = command.positionRequest; // the fingers arrive instantly
   nextStatus.current = kReportedCurrent;
   return nextStatus;
}
} // namespace Robotiq::fake
