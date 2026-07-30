// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief A Modbus RTU server in front of a RegisterModel.
//! The second of the three pieces the public makeFakeGripper() assembles —
//! behaviour (RegisterModel), protocol (this server), transport
//! (GripperSerial). Each does one job, so the exchange above them — framing,
//! CRC, the typed blocks, the exchange cycle — runs exactly as it does
//! against hardware.
//!
//! Internal to the library. The supported way to reach it is
//! Robotiq::makeFakeGripper().

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <Robotiq/detail/serial.hpp>

namespace Robotiq::fake {

class RegisterModel;

class GripperServer
{
public:
   //! \param model The device this server answers for; must outlive it.
   explicit GripperServer(RegisterModel& model, uint8_t slaveAddress);
   ~GripperServer();

   GripperServer(const GripperServer&) = delete;
   GripperServer& operator=(const GripperServer&) = delete;

   //! Hand over request bytes and answer them. Synchronous: the reply is
   //! ready to take when this returns.
   void deliver(const std::vector<uint8_t>& request);

   //! Take up to \p max bytes of the pending reply.
   [[nodiscard]] std::vector<uint8_t> drain(size_t max);

   //! Throw away any pending reply, as a link that lost it would.
   void discardPendingReply();

private:
   // nanomodbus is a vendored C library that is never installed, so it must
   // not appear in a header — not even this one. Hiding it here also matches
   // GripperModbusClient, which does the same on the real path.
   struct Impl;
   std::unique_ptr<Impl> _impl;
};

} // namespace Robotiq::fake
