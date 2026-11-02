// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "process_image.hpp"

#include <mutex> // std::lock_guard — available even where std::mutex is not

#include <Robotiq/gripper/driver_exception.hpp>

namespace Robotiq::detail {
namespace {
std::unique_ptr<Mutex> checkedMutex(Platform& platform)
{
   auto mutex = platform.makeMutex();
   if(!mutex)
   {
      throw DriverException("a Platform returned a null Mutex — the process image has nothing to lock");
   }
   return mutex;
}

std::unique_ptr<ConditionVariable> checkedConditionVariable(Platform& platform)
{
   auto condition = platform.makeConditionVariable();
   if(!condition)
   {
      throw DriverException("a Platform returned a null ConditionVariable — nothing can wait on the image");
   }
   return condition;
}
} // namespace

ProcessImage::ProcessImage(Platform& platform)
   : _mutex(checkedMutex(platform))
   , _statusRefreshed(checkedConditionVariable(platform))
{
}

void ProcessImage::seed(const GripperStatus& status,
                        std::chrono::steady_clock::time_point at,
                        const GripperCommand& command)
{
   const std::lock_guard<Mutex> lock(*_mutex);
   _stamped.metadata.exchangeCount = 0;
   _stamped.metadata.timestamp = at;
   _stamped.command = command;
   _stamped.status = status;
   _command = command;
}

void ProcessImage::setCommand(const GripperCommand& command)
{
   const std::lock_guard<Mutex> lock(*_mutex);
   _command = command;
}

GripperCommand ProcessImage::command() const
{
   const std::lock_guard<Mutex> lock(*_mutex);
   return _command;
}

GripperStatus ProcessImage::status() const
{
   const std::lock_guard<Mutex> lock(*_mutex);
   return _stamped.status;
}

StampedExchange ProcessImage::stampedExchange() const
{
   const std::lock_guard<Mutex> lock(*_mutex);
   return _stamped;
}

void ProcessImage::publish(const GripperCommand& command,
                           const GripperStatus& status,
                           std::chrono::steady_clock::time_point completedAt)
{
   {
      const std::lock_guard<Mutex> lock(*_mutex);
      ++_stamped.metadata.exchangeCount;
      _stamped.metadata.timestamp = completedAt;
      _stamped.command = command;
      _stamped.status = status;
   }
   _statusRefreshed->notifyAll();
}

StampedExchange ProcessImage::sync(uint64_t count, std::chrono::steady_clock::time_point deadline) const
{
   const std::lock_guard<Mutex> lock(*_mutex);
   while(_stamped.metadata.exchangeCount <= count && !_closed && std::chrono::steady_clock::now() < deadline)
   {
      _statusRefreshed->waitUntil(*_mutex, deadline);
   }
   return _stamped;
}

void ProcessImage::close() noexcept
{
   {
      const std::lock_guard<Mutex> lock(*_mutex);
      _closed = true;
   }
   _statusRefreshed->notifyAll();
}

} // namespace Robotiq::detail
