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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// Recent mingw-w64 libstdc++ predefines NOMINMAX; redefining it is a
// warning, fatal under -Werror.
#ifndef NOMINMAX
#define NOMINMAX
#endif
// mingw-w64 defaults to 0x0502, behind which its headers hide
// CreateWaitableTimerExW
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10
#endif
#include <windows.h>
#endif

#include <Robotiq/gripper/platform.hpp>

namespace Robotiq {
namespace {

#ifdef _WIN32
// Two thresholds: SDK headers gained this constant in 10.0.18362 (1903);
// the OS honors the flag from Windows 10 1803 (probed at runtime below,
// falling back to the std sleep on older kernels).
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// std::this_thread's sleeps bottom out in Sleep(), which is quantized by
// the OS timer tick (~15.6 ms by default) — an exchange-rate ceiling near
// 64 Hz. A high-resolution waitable timer paces at sub-millisecond
// granularity without raising the machine-wide tick.
class WaitableTimer
{
public:
   // Null on a pre-1803 kernel, which rejects the flag; the caller falls back.
   WaitableTimer()
      : _timer(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS))
   {
   }

   WaitableTimer(const WaitableTimer&) = delete;
   WaitableTimer& operator=(const WaitableTimer&) = delete;

   ~WaitableTimer()
   {
      if(_timer != nullptr)
      {
         CloseHandle(_timer);
      }
   }

   // False when the wait could not be armed; the caller falls back.
   bool sleepUntil(std::chrono::steady_clock::time_point timePoint)
   {
      if(_timer == nullptr)
      {
         return false;
      }
      // A wait may end early; loop until the deadline has actually passed.
      for(;;)
      {
         const auto remaining = timePoint - std::chrono::steady_clock::now();
         if(remaining <= remaining.zero())
         {
            return true;
         }
         LARGE_INTEGER dueTime; // negative: relative, in 100 ns units
         dueTime.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100 - 1;
         if(SetWaitableTimer(_timer, &dueTime, 0, nullptr, nullptr, FALSE) == 0
            || WaitForSingleObject(_timer, INFINITE) != WAIT_OBJECT_0)
         {
            return false;
         }
      }
   }

private:
   HANDLE _timer;
};
#endif

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
#ifdef _WIN32
      // Per-thread: the exchange thread paces while blocked procedures poll.
      thread_local WaitableTimer timer;
      if(timer.sleepUntil(timePoint))
      {
         return;
      }
#endif
      // Looped, and on the steady-clock remainder: some STLs time
      // sleep_until against the system clock, where a backward step ends the
      // sleep early and a forward step would busy-spin a sleep_until loop.
      for(auto remaining = timePoint - std::chrono::steady_clock::now(); remaining > remaining.zero();
          remaining = timePoint - std::chrono::steady_clock::now())
      {
         std::this_thread::sleep_for(remaining);
      }
   }

   void sleepFor(std::chrono::milliseconds duration) override
   {
      sleepUntil(std::chrono::steady_clock::now() + duration);
   }
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
