// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Reference Platform implementation over Azure RTOS ThreadX.
//! Not compiled into the SDK: use it from your firmware project with ThreadX
//! (tx_api.h) on the include path, and pass an instance to Gripper's
//! platform-taking constructor. Porting to another RTOS means implementing
//! the same four Platform members over the native primitives — this file is
//! the template.
//!
//! Construct after the ThreadX kernel is running (the tx_*_create calls need
//! an initialized kernel): create the Gripper from within a task, never at
//! static-init time.
//!
//! Integration caveats (each cost real debugging time during STM32N6
//! bring-up, and both present as an unexplained hang rather than an error):
//!  - The injected Serial::read MUST yield the CPU while awaiting bytes —
//!    e.g. interrupt/DMA completion signalled through a ThreadX semaphore. A
//!    busy-wait read (such as a polled HAL_UART_Receive) runs at the exchange
//!    task's priority and will STARVE lower-priority tasks — including the
//!    app task that drives activate()/setCommand — hanging the application.
//!    This is not optional on a preemptive RTOS.
//!  - std::chrono::steady_clock must be backed by a monotonic tick source.
//!    Which libc call libstdc++ uses for it is toolchain/multilib dependent —
//!    clock_gettime(CLOCK_MONOTONIC) OR gettimeofday(); confirm in the
//!    disassembly and back that one. On arm-none-eabi 13.x / v8-m.main it was
//!    gettimeofday(). A stubbed backing silently freezes steady_clock, which
//!    breaks every timeout and the exchange pacing (the failure looks like a
//!    hang inside activate(), not a clock bug).

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "tx_api.h"

#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/driver_exception.hpp>

namespace Robotiq::ports {

class ThreadXMutex final : public Mutex
{
public:
   ThreadXMutex()
   {
      if(tx_mutex_create(&_mutex, const_cast<CHAR*>("grippers"), TX_NO_INHERIT) != TX_SUCCESS)
      {
         throw DriverException("tx_mutex_create failed (is the ThreadX kernel running?)");
      }
   }

   ~ThreadXMutex() override { tx_mutex_delete(&_mutex); }

   ThreadXMutex(const ThreadXMutex&) = delete;
   ThreadXMutex& operator=(const ThreadXMutex&) = delete;

   void lock() override { tx_mutex_get(&_mutex, TX_WAIT_FOREVER); }
   void unlock() override { tx_mutex_put(&_mutex); }

private:
   TX_MUTEX _mutex{};
};

class ThreadXThread final : public Thread
{
public:
   ThreadXThread(std::function<void()> fn, ULONG stackSize, UINT priority)
      : _fn(std::make_unique<std::function<void()>>(std::move(fn)))
      , _stack(std::make_unique<std::uint8_t[]>(stackSize))
      , _tcb(std::make_unique<TX_THREAD>())
   {
      static_assert(sizeof(ULONG) >= sizeof(void*), "the entry argument must be able to carry a pointer");
      const UINT status = tx_thread_create(_tcb.get(),
                                           const_cast<CHAR*>("gripper"),
                                           &ThreadXThread::entry,
                                           reinterpret_cast<ULONG>(_fn.get()),
                                           _stack.get(),
                                           stackSize,
                                           priority,
                                           priority,
                                           TX_NO_TIME_SLICE,
                                           TX_AUTO_START);
      if(status != TX_SUCCESS)
      {
         // Failing loudly here is what keeps join() well-defined: it never
         // has to poll a task that was never created.
         throw DriverException("tx_thread_create failed with status " + std::to_string(status)
                               + " (is the ThreadX kernel running, and the priority in range?)");
      }
   }

   // Destroying a still-running task is a logic error (std::thread would
   // std::terminate()); terminate it defensively before deleting.
   ~ThreadXThread() override
   {
      if(!_joined)
      {
         tx_thread_terminate(_tcb.get());
      }
      tx_thread_delete(_tcb.get());
   }

   ThreadXThread(const ThreadXThread&) = delete;
   ThreadXThread& operator=(const ThreadXThread&) = delete;

   // Block until the task's entry returns. Granularity: one ThreadX tick.
   void join() override
   {
      UINT state = TX_READY;
      while(true)
      {
         // nullptr, not TX_NULL: C++ won't convert (void*)0 to the typed out-params.
         tx_thread_info_get(_tcb.get(), nullptr, &state, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
         if(state == TX_COMPLETED || state == TX_TERMINATED)
         {
            break;
         }
         tx_thread_sleep(1);
      }
      _joined = true;
   }

private:
   static VOID entry(ULONG arg) { (*reinterpret_cast<std::function<void()>*>(arg))(); }

   std::unique_ptr<std::function<void()>> _fn;
   std::unique_ptr<std::uint8_t[]> _stack;
   std::unique_ptr<TX_THREAD> _tcb;
   bool _joined = false;
};

//! \brief Platform over ThreadX tasks, mutexes and tx_thread_sleep.
class ThreadXPlatform final : public Platform
{
public:
   // Stack size and priority for the exchange task — app-dependent: the
   // exchange loop uses exceptions, so keep the stack generous, and pick the
   // priority relative to your app task deliberately (both must make
   // progress).
   explicit ThreadXPlatform(ULONG threadStackSize = 4096u, UINT threadPriority = 15u)
      : _threadStackSize(threadStackSize)
      , _threadPriority(threadPriority)
   {
   }

   std::unique_ptr<Mutex> makeMutex() override { return std::make_unique<ThreadXMutex>(); }

   std::unique_ptr<Thread> spawn(std::function<void()> fn) override
   {
      return std::make_unique<ThreadXThread>(std::move(fn), _threadStackSize, _threadPriority);
   }

   // The waits yield the CPU via the ThreadX scheduler; granularity is one
   // ThreadX tick, and a nonzero wait is rounded up so it never busy-spins.
   void sleepUntil(std::chrono::steady_clock::time_point timePoint) override
   {
      const auto now = std::chrono::steady_clock::now();
      if(timePoint <= now)
      {
         return;
      }
      tx_thread_sleep(ticksFor(timePoint - now));
   }

   void sleepFor(std::chrono::milliseconds duration) override
   {
      if(duration <= std::chrono::milliseconds::zero())
      {
         return;
      }
      tx_thread_sleep(ticksFor(duration));
   }

private:
   static ULONG ticksFor(std::chrono::nanoseconds duration)
   {
      const long long ns = duration.count();
      const long long perSec = TX_TIMER_TICKS_PER_SECOND;
      const long long ticks = (ns * perSec + 999999999LL) / 1000000000LL;
      return ticks < 1 ? 1UL : static_cast<ULONG>(ticks);
   }

   ULONG _threadStackSize;
   UINT _threadPriority;
};

} // namespace Robotiq::ports
