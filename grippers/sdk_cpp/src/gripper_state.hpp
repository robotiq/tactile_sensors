// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Internal: the link, the exchange thread and the process image
//! behind Gripper. Its own translation unit, so gripper.cpp is left with the
//! public class and the procedures composed over it.

#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/serial.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/detail/throttle.hpp>
#include <Robotiq/gripper/modbus_client.hpp>

namespace Robotiq::detail {

struct GripperState
{
   std::shared_ptr<Logger> logger;
   std::shared_ptr<Platform> platform;
   GripperModbusClient client;
   detail::Throttle failureLogThrottle{std::chrono::milliseconds(1000)};
   std::chrono::microseconds period;

   const std::unique_ptr<Mutex> imageMutex;
   GripperCommand command{};
   GripperStatus status{};

   std::atomic<ConnectionState> state{ConnectionState::Connecting};
   std::atomic<bool> running{false};
   // 32-bit: a 64-bit atomic needs __atomic_*_8 (no native 8-byte atomic on a
   // 32-bit MCU); a failure counter never needs more than 32 bits.
   std::atomic<uint32_t> consecutiveFailures{0};
   std::unique_ptr<Thread> exchangeThread;

   GripperState(std::unique_ptr<Serial> serial,
                uint8_t slaveAddress,
                std::chrono::microseconds exchangePeriod,
                std::shared_ptr<Platform> os,
                std::shared_ptr<Logger> log);

   // RAII: the state can never outlive its exchange thread.
   ~GripperState();

   // Initialize the command image using the status echoes. Speed and force
   // have no echoes; they are initialized at maximum values, matching the
   // urcap driver behavior.
   void initializeImage();

   void exchangeOnce();

   void start();

   void stop() noexcept;
};

} // namespace Robotiq::detail
