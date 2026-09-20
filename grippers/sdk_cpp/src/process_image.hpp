// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Internal: the command and status blocks the exchange cycle carries,
//! with the count and instant that name the status, behind one lock. Every
//! method takes the lock on entry, so nothing outside can read a status paired
//! with another cycle's count or publish one half of a snapshot.

#pragma once

#include <chrono>
#include <memory>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/platform.hpp>
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
   [[nodiscard]] StampedStatus stampedStatus() const;

   void publish(const GripperStatus& status, std::chrono::steady_clock::time_point completedAt);

   [[nodiscard]] StampedStatus sync(uint64_t count, std::chrono::steady_clock::time_point deadline) const;

   void close() noexcept;

private:
   const std::unique_ptr<Mutex> _mutex;
   const std::unique_ptr<ConditionVariable> _statusRefreshed;
   GripperCommand _command{};
   StampedStatus _stamped{};
   bool _closed = false;
};

} // namespace Robotiq::detail
