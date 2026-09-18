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

//! Platform that delegates to the default std-backed one but counts
//! what the gripper asks of it — the seam a real RTOS port implements.
class InstrumentedPlatform : public Platform
{
public:
   std::atomic<int> mutexesCreated{0};
   std::atomic<int> threadsSpawned{0};
   std::atomic<int> threadsJoined{0};
   std::atomic<int> sleepUntils{0};
   std::atomic<int> sleepFors{0};

   std::unique_ptr<Mutex> makeMutex() override
   {
      ++mutexesCreated;
      return _real->makeMutex();
   }

   std::unique_ptr<ConditionVariable> makeConditionVariable() override { return _real->makeConditionVariable(); }

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

private:
   std::shared_ptr<Platform> _real = makeDefaultPlatform();
};

} // namespace Robotiq::test
