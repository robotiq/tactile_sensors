// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief A Modbus RTU server in front of a RegisterModel, and the
//!        Serial transport that reaches it.
//! Together with the model these are the three pieces the public
//! makeFakeGripper() assembles: behaviour (the model), protocol (this
//! server), transport (the serial). Each does one job, so the exchange above
//! them — framing, CRC, the typed blocks, the exchange cycle — runs exactly
//! as it does against hardware.
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

//! Serial transport over a GripperServer, synchronous by construction:
//! each write delivers a request and runs the server, and reads drain the
//! reply it produced.
class GripperSerial : public detail::Serial
{
public:
   explicit GripperSerial(GripperServer& gripperServer);

   void open() override;
   [[nodiscard]] bool isOpen() const override;
   void close() override;

   [[nodiscard]] std::vector<uint8_t> read(size_t size, std::chrono::milliseconds timeout) override;
   void write(const std::vector<uint8_t>& data) override;

   [[nodiscard]] std::chrono::milliseconds getTimeout() const override;

protected:
   GripperServer& _gripperServer;

private:
   bool _open = false;
};
} // namespace Robotiq::fake
