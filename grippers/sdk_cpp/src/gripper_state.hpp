// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Internal: the link and the exchange thread behind Gripper, driving
//! a ProcessImage. The image owns its own lock, so nothing here reaches past
//! it — see process_image.hpp.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/serial.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/detail/throttle.hpp>
#include <Robotiq/gripper/modbus_client.hpp>

#include "process_image.hpp"

namespace Robotiq::detail {

class GripperState
{
public:
   GripperState(std::unique_ptr<Serial> serial,
                uint8_t slaveAddress,
                std::chrono::microseconds exchangePeriod,
                std::shared_ptr<Platform> os,
                std::shared_ptr<Logger> log);

   // RAII: the state can never outlive its exchange thread.
   ~GripperState();

   GripperState(const GripperState&) = delete;
   GripperState& operator=(const GripperState&) = delete;

   // Initialize the command image using the status echoes. Speed and force
   // have no echoes; they are initialized at maximum values, matching the
   // urcap driver behavior.
   void initializeImage();

   void start();
   void stop() noexcept;

   void setCommand(const GripperCommand& command);
   [[nodiscard]] GripperCommand command() const;
   [[nodiscard]] GripperStatus status() const;
   [[nodiscard]] StampedExchange stampedExchange() const;

   // The image once its count reaches \p desiredExchangeCount, or as it
   // stands when \p deadline passes.
   [[nodiscard]] StampedExchange waitForExchange(uint64_t desiredExchangeCount,
                                                 std::chrono::steady_clock::time_point deadline) const;

   [[nodiscard]] ConnectionState connectionState() const { return _connectionState.load(); }
   [[nodiscard]] Platform& platform() const noexcept { return *_platform; }

private:
   void exchangeOnce();

   std::shared_ptr<Logger> _logger;
   std::shared_ptr<Platform> _platform;
   GripperModbusClient _client;
   detail::Throttle _failureLogThrottle{std::chrono::milliseconds(1000)};
   std::chrono::microseconds _period;

   ProcessImage _image;

   std::atomic<ConnectionState> _connectionState{ConnectionState::Connecting};
   std::atomic<bool> _running{false};
   // 32-bit: a 64-bit atomic needs __atomic_*_8 (no native 8-byte atomic on a
   // 32-bit MCU); a failure counter never needs more than 32 bits.
   std::atomic<uint32_t> _consecutiveFailures{0};
   std::unique_ptr<Thread> _exchangeThread;
};

} // namespace Robotiq::detail
