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
   _stamped = {0, status, at};
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

StampedStatus ProcessImage::stampedStatus() const
{
   const std::lock_guard<Mutex> lock(*_mutex);
   return _stamped;
}

void ProcessImage::publish(const GripperStatus& status, std::chrono::steady_clock::time_point completedAt)
{
   {
      const std::lock_guard<Mutex> lock(*_mutex);
      _stamped = {_stamped.exchangeCount + 1, status, completedAt};
   }
   _statusRefreshed->notifyAll();
}

void ProcessImage::wakeAll() noexcept
{
   _statusRefreshed->notifyAll();
}

} // namespace Robotiq::detail
