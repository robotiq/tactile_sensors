// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "fake_gripper_fixture.hpp"

#include "fake/status_writer.hpp"

namespace Robotiq::test {

void InstrumentedRegisterModel::setActivated()
{
   GripperStatus newStatus = status();
   fake::setActivated(newStatus, true);
   fake::setActivationState(newStatus, ActivationState::Complete);
   setStatus(newStatus);
   setActivationDone(true);
}

void InstrumentedRegisterModel::setFault(GripperFault fault)
{
   GripperStatus newStatus = status();
   fake::setGripperFault(newStatus, fault);
   setStatus(newStatus);
}

void InstrumentedRegisterModel::setGoToEcho()
{
   GripperStatus newStatus = status();
   fake::setGoToEnabled(newStatus, true);
   setStatus(newStatus);
}

void InstrumentedRegisterModel::setPositionRequestEcho(uint8_t position)
{
   GripperStatus newStatus = status();
   newStatus.positionRequestEcho = position;
   setStatus(newStatus);
}

void InstrumentedRegisterModel::setActivationDone(bool value)
{
   _previousActivateBit = value;
   _activationDone = value;
}

void InstrumentedRegisterModel::read(uint16_t address, uint16_t quantity, uint16_t* out) const
{
   statusReads.fetch_add(1);
   RegisterModel::read(address, quantity, out);
}

void InstrumentedRegisterModel::write(uint16_t address, uint16_t quantity, const uint16_t* values)
{
   commandWrites.fetch_add(1);
   RegisterModel::write(address, quantity, values);
}

GripperStatus InstrumentedRegisterModel::processCommand(const GripperCommand& command,
                                                        const GripperStatus& currentStatus)
{
   // The rACT falling edge is the reset request. Derived here rather than
   // signalled by the model, so the model owes tests no hook at all.
   if(_previousActivateBit && !command.action.get(ActionRequestBit::Activate))
   {
      resets.fetch_add(1);
   }
   // The model runs even while pinned: it is what tracks the rACT edge, so
   // skipping it would leave a stale edge behind once the pin is released.
   const GripperStatus modelled = RegisterModel::processCommand(command, currentStatus);
   return pinnedStatus ? *pinnedStatus : modelled;
}
} // namespace Robotiq::test
