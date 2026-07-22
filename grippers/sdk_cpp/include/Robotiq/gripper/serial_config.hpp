// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Serial link parameters, fixed at construction of a Serial
//!        implementation.

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace Robotiq {
struct SerialConfig
{
   // Serial port: "/dev/ttyUSB0" (Linux), "/dev/tty.usbserial-XXXX" (macOS),
   // "COM3" (Windows).
   std::string port;

   // Wire baud rate; must match the gripper's persisted setting
   // (factory default 115200).
   uint32_t baudrate = 115200;

   // Per-transaction timeout for blocking reads and writes.
   std::chrono::milliseconds timeout{500};

   // FTDI latency_timer enforced on open (Linux sysfs only; 0 disables).
   // The kernel default of 16 ms silently triples Modbus cycle latency.
   int latencyTimerMs = 1;
};
} // namespace Robotiq
