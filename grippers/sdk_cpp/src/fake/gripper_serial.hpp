// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The Serial transport that reaches a fake gripper's Modbus server.
//! The third of the three pieces the public makeFakeGripper() assembles —
//! behaviour (RegisterModel), protocol (GripperServer), transport (this) — so
//! the exchange above them runs exactly as it does against hardware.
//!
//! Internal to the library. The supported way to reach it is
//! Robotiq::makeFakeGripper().

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <Robotiq/detail/serial.hpp>

namespace Robotiq::fake {

class GripperServer;

//! Synchronous by construction: each write delivers a request and runs the
//! server, and reads drain the reply it produced.
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
