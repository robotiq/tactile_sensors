// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Internal: the command block for the next exchange and the exchange of
//! the last one, behind one lock. Every method takes the lock on entry, so
//! nothing outside can read a status paired with another cycle's count or
//! command, or publish one half of a record.

#pragma once

#include <chrono>
#include <memory>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/stamped_exchange.hpp>
#include <Robotiq/gripper/status.hpp>

namespace Robotiq::detail {

class ProcessImage
{
public:
   explicit ProcessImage(Platform& platform);

   ProcessImage(const ProcessImage&) = delete;
   ProcessImage& operator=(const ProcessImage&) = delete;

   void seed(const GripperStatus& status, std::chrono::steady_clock::time_point at, const GripperCommand& command);

   void setCommand(const GripperCommand& command);
   [[nodiscard]] GripperCommand command() const;
   [[nodiscard]] GripperStatus status() const;
   [[nodiscard]] StampedExchange stampedExchange() const;

   // Record the exchange that wrote \p command and read \p status back.
   void publish(const GripperCommand& command,
                const GripperStatus& status,
                std::chrono::steady_clock::time_point completedAt);

   [[nodiscard]] StampedExchange sync(uint64_t count, std::chrono::steady_clock::time_point deadline) const;

   // Final: every waiter returns at once, and so does every later sync().
   // Nothing reopens an image — a GripperState stops once, in its
   // destructor — so a closed image is not reused.
   void close() noexcept;

private:
   const std::unique_ptr<Mutex> _mutex;
   const std::unique_ptr<ConditionVariable> _statusRefreshed;
   GripperCommand _command{};
   StampedExchange _stamped{};
   bool _closed = false;
};

} // namespace Robotiq::detail
