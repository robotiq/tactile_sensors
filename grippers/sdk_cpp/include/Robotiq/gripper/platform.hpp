// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The OS services Gripper's threaded runtime needs, as an injectable
//! interface: one exchange thread, one lock, and a yielding sleep.
//! Every Gripper runs on one: hosted applications pass
//! makeDefaultPlatform(); an RTOS target implements this over the native
//! primitives — ports/threadx/threadx_platform.hpp is the working
//! reference. A single-threaded target (bare superloop) should skip Gripper
//! entirely and drive detail::GripperModbusClient itself — that layer needs
//! no Platform.
//!
//! Concurrency contract for implementations: the sleeps may be called from
//! several threads at once (the exchange thread paces with sleepUntil while
//! a blocked procedure like activate() polls with sleepFor); makeMutex and
//! spawn are called during gripper construction only.
//!
//! RTOS-specific integration caveats (a yielding Serial::read, the backing
//! of steady_clock, task priorities) are documented in the reference port,
//! ports/threadx/threadx_platform.hpp — read them before implementing this
//! interface for an RTOS.

#pragma once

#include <chrono>
#include <functional>
#include <memory>

namespace Robotiq {

//! \brief A lock. BasicLockable, so it drops straight into std::lock_guard.
class Mutex
{
public:
   virtual ~Mutex() = default;

   virtual void lock() = 0;
   virtual void unlock() = 0;
};

//! \brief A running thread.
class Thread
{
public:
   virtual ~Thread() = default;

   // Block until the thread's entry function returns.
   virtual void join() = 0;
};

//! \brief Factory + sleeps: what Gripper asks of the operating system.
class Platform
{
public:
   virtual ~Platform() = default;

   [[nodiscard]] virtual std::unique_ptr<Mutex> makeMutex() = 0;

   // Start a thread running fn. fn returns when its owner stops it; the
   // returned handle is then join()ed before destruction.
   [[nodiscard]] virtual std::unique_ptr<Thread> spawn(std::function<void()> fn) = 0;

   // Sleep the calling thread until a monotonic time point, yielding the CPU.
   virtual void sleepUntil(std::chrono::steady_clock::time_point timePoint) = 0;

   // Sleep the calling thread for a duration, yielding the CPU.
   virtual void sleepFor(std::chrono::milliseconds duration) = 0;
};

// The std::thread-backed platform (one shared instance). Hosted-only:
// freestanding builds leave its TU out, so calling this there fails to
// link — construct your RTOS platform instead.
[[nodiscard]] std::shared_ptr<Platform> makeDefaultPlatform();

} // namespace Robotiq
