// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// The std::thread-backed Platform for hosted runtimes. Freestanding
// builds leave this TU out and pass a Platform explicitly — Gripper's
// platform default argument is bound at hosted call sites, never here.

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <Robotiq/gripper/platform.hpp>

namespace Robotiq {
namespace {

class StdMutex final : public Mutex
{
public:
   void lock() override { _mutex.lock(); }
   void unlock() override { _mutex.unlock(); }

private:
   std::mutex _mutex;
};

class StdThread final : public Thread
{
public:
   explicit StdThread(std::function<void()> fn)
      : _thread(std::move(fn))
   {
   }

   // std::thread would std::terminate() on a joinable destruction; joining
   // is always right here, since the owner has already stopped the loop.
   ~StdThread() override { join(); }

   void join() override
   {
      if(_thread.joinable())
      {
         _thread.join();
      }
   }

private:
   std::thread _thread;
};

class StdPlatform final : public Platform
{
public:
   std::unique_ptr<Mutex> makeMutex() override { return std::make_unique<StdMutex>(); }

   std::unique_ptr<Thread> spawn(std::function<void()> fn) override
   {
      return std::make_unique<StdThread>(std::move(fn));
   }

   void sleepUntil(std::chrono::steady_clock::time_point timePoint) override
   {
      std::this_thread::sleep_until(timePoint);
   }

   void sleepFor(std::chrono::milliseconds duration) override { std::this_thread::sleep_for(duration); }
};

} // namespace

std::shared_ptr<Platform> makeDefaultPlatform()
{
   // One shared instance: the platform is stateless, so every default user
   // can share it.
   static const auto instance = std::make_shared<StdPlatform>();
   return instance;
}
} // namespace Robotiq
