// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

#include <Robotiq/gripper/serial_io_exception.hpp>

#include "fake_gripper_fixture.hpp"
#include "instrumented_platform.hpp"

namespace Robotiq::test {

inline constexpr uint8_t kSlave = 0x09;
inline constexpr std::chrono::milliseconds kFastPeriod{1};

//! GripperSerial whose writes fail while failing is set — a link
//! that starts healthy, drops out, and comes back. Failing on write
//! keeps the fake's reply streams free of stale responses.
class WriteFailingSerial : public fake::GripperSerial
{
public:
   using fake::GripperSerial::GripperSerial;

   void write(const std::vector<uint8_t>& data) override
   {
      if(failing.load())
      {
         throw SerialIOException("injected wire failure");
      }
      GripperSerial::write(data);
   }

   std::atomic<bool> failing{false};
};

} // namespace Robotiq::test
