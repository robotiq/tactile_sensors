// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <utility>

#include <Robotiq/gripper/platform.hpp>

namespace Robotiq::test {

//! Thread that counts its joins on the way through to the real one.
class JoinCountingThread : public Thread
{
public:
   JoinCountingThread(std::unique_ptr<Thread> real, std::atomic<int>& joins)
      : _real(std::move(real))
      , _joins(joins)
   {
   }

   void join() override
   {
      _real->join();
      ++_joins;
   }

private:
   std::unique_ptr<Thread> _real;
   std::atomic<int>& _joins;
};

//! Condition variable that counts the waits it serves, on the way through
//! to the real one.
class WaitCountingConditionVariable : public ConditionVariable
{
public:
   WaitCountingConditionVariable(std::unique_ptr<ConditionVariable> real, std::atomic<int>& waits)
      : _real(std::move(real))
      , _waits(waits)
   {
   }

   void waitUntil(Mutex& mutex, std::chrono::steady_clock::time_point timePoint) override
   {
      ++_waits;
      _real->waitUntil(mutex, timePoint);
   }

   void notifyAll() noexcept override { _real->notifyAll(); }

private:
   std::unique_ptr<ConditionVariable> _real;
   std::atomic<int>& _waits;
};

//! Platform that delegates to the default std-backed one but counts
//! what the gripper asks of it — the seam a real RTOS port implements.
class InstrumentedPlatform : public Platform
{
public:
   std::atomic<int> mutexesCreated{0};
   std::atomic<int> conditionVariablesCreated{0};
   std::atomic<int> conditionWaits{0};
   std::atomic<int> threadsSpawned{0};
   std::atomic<int> threadsJoined{0};
   std::atomic<int> sleepUntils{0};
   std::atomic<int> sleepFors{0};

   std::unique_ptr<Mutex> makeMutex() override
   {
      ++mutexesCreated;
      return _real->makeMutex();
   }

   std::unique_ptr<ConditionVariable> makeConditionVariable() override
   {
      ++conditionVariablesCreated;
      auto counting = std::make_unique<WaitCountingConditionVariable>(_real->makeConditionVariable(), conditionWaits);
      _created.store(counting.get());
      return counting;
   }

   std::unique_ptr<Thread> spawn(std::function<void()> fn) override
   {
      ++threadsSpawned;
      return std::make_unique<JoinCountingThread>(_real->spawn(std::move(fn)), threadsJoined);
   }

   void sleepUntil(std::chrono::steady_clock::time_point timePoint) override
   {
      ++sleepUntils;
      _real->sleepUntil(timePoint);
   }

   void sleepFor(std::chrono::milliseconds duration) override
   {
      ++sleepFors;
      _real->sleepFor(duration);
   }

   //! Notify the gripper's condition variable with nothing behind it — the
   //! spurious wake every waiter has to be ready for.
   void notifyWithNoNews()
   {
      if(ConditionVariable* const condition = _created.load())
      {
         condition->notifyAll();
      }
   }

private:
   std::atomic<ConditionVariable*> _created{nullptr};
   std::shared_ptr<Platform> _real = makeDefaultPlatform();
};

} // namespace Robotiq::test
