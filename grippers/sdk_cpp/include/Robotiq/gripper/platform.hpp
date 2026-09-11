// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <chrono>
#include <functional>
#include <memory>

namespace Robotiq {

//! \ingroup platform
//! \brief A lockable object. BasicLockable, so it drops straight into std::lock_guard.
class Mutex
{
public:
   virtual ~Mutex() = default;

   //! Blocks until the lockable object is acquired.
   virtual void lock() = 0;
   //! Releases a lockable object previously acquired with lock().
   virtual void unlock() = 0;
};

//! \ingroup platform
//! \brief A running thread.
class Thread
{
public:
   virtual ~Thread() = default;

   //! Blocks until the thread's entry function returns.
   virtual void join() = 0;
};

//! \ingroup platform
//! \brief The OS services Gripper's threaded runtime needs, as an
//! injectable interface: one exchange thread, one lockable object, and a yielding
//! sleep.
//!
//! Every Gripper runs on one: hosted applications pass
//! makeDefaultPlatform(); an RTOS target implements this over the native
//! primitives — ports/threadx/threadx_platform.hpp is the working
//! reference. A single-threaded target (bare superloop) should skip
//! Gripper entirely and drive GripperModbusClient itself — that layer
//! needs no Platform. Read that class's warning first: it is the
//! exception, not an alternative.
//!
//! **Concurrency contract for implementations:** the sleeps may be
//! called from several threads at once (the exchange thread paces with
//! sleepUntil() while a blocked procedure like activate() polls with
//! sleepFor()); makeMutex() and spawn() are called during gripper
//! construction only.
//!
//! RTOS-specific integration caveats (a yielding Serial::read, the
//! backing of steady_clock, task priorities) are documented in the
//! reference port, ports/threadx/threadx_platform.hpp — read them before
//! implementing this interface for an RTOS.
class Platform
{
public:
   virtual ~Platform() = default;

   //! \return A newly constructed lockable object.
   [[nodiscard]] virtual std::unique_ptr<Mutex> makeMutex() = 0;

   //! \brief Start a thread running \p fn.
   //!
   //! \p fn returns when its owner stops it; the returned handle is then
   //! join()ed before destruction.
   //! \param fn The thread's entry function.
   //! \return A handle to the running thread.
   [[nodiscard]] virtual std::unique_ptr<Thread> spawn(std::function<void()> fn) = 0;

   //! \brief Sleep the calling thread until a monotonic time point, yielding the CPU.
   //! \param timePoint The point in time to sleep until.
   virtual void sleepUntil(std::chrono::steady_clock::time_point timePoint) = 0;

   //! \brief Sleep the calling thread for a duration, yielding the CPU.
   //! \param duration How long to sleep.
   virtual void sleepFor(std::chrono::milliseconds duration) = 0;
};

//! \ingroup platform
//! \brief The std::thread-backed platform (one shared instance).
//!
//! Hosted-only: freestanding builds leave its TU out, so calling this
//! there fails to link — construct your RTOS platform instead.
//! \return The shared default Platform instance.
[[nodiscard]] std::shared_ptr<Platform> makeDefaultPlatform();

} // namespace Robotiq
