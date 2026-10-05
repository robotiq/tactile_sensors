// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

#include <Robotiq/detail/config.hpp>
#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/stamped_exchange.hpp>

namespace Robotiq {
namespace detail {
//! \cond DOXYGEN_EXCLUDE
// Now plus the timeout, capped so that the sum, and a platform's conversion
// of the deadline to another clock, cannot overflow on milliseconds::max().
inline std::chrono::steady_clock::time_point deadlineAfter(std::chrono::milliseconds timeout)
{
   using namespace std::chrono;
   constexpr auto longest = duration_cast<milliseconds>(steady_clock::duration::max()) / 2;
   return steady_clock::now() + std::min(timeout, longest);
}

template <typename Predicate>
std::optional<StampedExchange> waitForExchangeAfter(const Gripper& gripper,
                                                    uint64_t afterCount,
                                                    Predicate predicate,
                                                    std::chrono::milliseconds timeout)
{
   const auto deadline = deadlineAfter(timeout);
   uint64_t desired = afterCount + 1;
   while(true)
   {
      const auto now = std::chrono::steady_clock::now();
      const auto remaining =
         deadline > now ? std::chrono::ceil<std::chrono::milliseconds>(deadline - now) : std::chrono::milliseconds(0);
      std::optional<StampedExchange> exchange = gripper.waitForExchangeCount(desired, remaining);
      if(!exchange || predicate(std::as_const(*exchange)))
      {
         return exchange;
      }
      desired = exchange->metadata.exchangeCount + 1;
   }
}

// The body of the deprecated polling forms, kept out of them so that they
// do not warn about each other.
template <typename Predicate>
bool pollUntil(Predicate predicate,
               Platform& platform,
               std::chrono::steady_clock::time_point deadline,
               std::chrono::milliseconds pollPeriod)
{
   while(true)
   {
      if(predicate())
      {
         return true;
      }
      if(std::chrono::steady_clock::now() >= deadline)
      {
         return false;
      }
      platform.sleepFor(pollPeriod);
   }
}
//! \endcond
} // namespace detail

//! \ingroup wait
//! \brief Wait, exchange by exchange, until \p predicate holds for one.
//!
//! \p predicate runs on each exchange completed after the call, in order,
//! as long as the caller keeps up with the cycle; one that fell behind gets
//! the newest and never sees the ones in between.
//! \tparam Predicate A callable taking a const StampedExchange&, returning bool.
//! \param gripper The gripper whose exchanges to wait on.
//! \param predicate The condition to wait for.
//! \param timeout How long to wait, starting now; 30 s by default.
//! \return The first exchange \p predicate held for; empty when none did
//!         before \p timeout.
template <typename Predicate>
std::optional<StampedExchange> waitFor(const Gripper& gripper,
                                       Predicate predicate,
                                       std::chrono::milliseconds timeout = std::chrono::seconds(30))
{
   return detail::waitForExchangeAfter(gripper,
                                       gripper.getMostRecentStampedExchange().metadata.exchangeCount,
                                       std::move(predicate),
                                       timeout);
}

//! \ingroup wait
//! \brief Set \p command and wait for the exchange that sends it.
//!
//! \param gripper The gripper to command.
//! \param command The whole command block to transmit; see GripperCommand.
//! \param timeout How long to wait for an exchange to carry it.
//! \return The first exchange that carried the block; empty when none did
//!         before \p timeout.
std::optional<StampedExchange> setCommandAndWaitForExchange(
   Gripper& gripper,
   const GripperCommand& command,
   std::chrono::milliseconds timeout = std::chrono::seconds(30));

//! \ingroup wait
//! \brief Wait for the gripper to adopt \p position as its setpoint.
//!
//! The gripper reports the position request it acts on in its status
//! (`positionRequestEcho`); the echo confirms the request reached it and
//! became the setpoint.
//! \param gripper The gripper whose exchanges to wait on.
//! \param position The position request to wait for.
//! \param timeout How long to wait, starting now.
//! \return The first exchange whose status echoes \p position; empty when
//!         none did before \p timeout.
std::optional<StampedExchange> waitForPositionEcho(const Gripper& gripper,
                                                   uint8_t position,
                                                   std::chrono::milliseconds timeout = std::chrono::seconds(30));

//! \ingroup wait
//! \brief Wait for the exchange on which the gripper reports \p detection.
//!
//! \param gripper The gripper whose exchanges to wait on.
//! \param detection The object-detection state to wait for; see ObjectDetection.
//! \param timeout How long to wait, starting now.
//! \return The first exchange whose status reports \p detection; empty when
//!         none did before \p timeout.
std::optional<StampedExchange> waitForObjectDetection(const Gripper& gripper,
                                                      ObjectDetection detection,
                                                      std::chrono::milliseconds timeout = std::chrono::seconds(30));

//! \ingroup wait
//! \brief Wait for the gripper motion to stop.
//!
//! \param gripper The gripper whose exchanges to wait on.
//! \param timeout How long to wait, starting now.
//! \return The first exchange whose status is no longer ObjectDetection::Moving;
//!         empty when none was before \p timeout.
std::optional<StampedExchange> waitForMotionEnd(const Gripper& gripper,
                                                std::chrono::milliseconds timeout = std::chrono::seconds(30));

//! \ingroup wait
//! \brief Poll \p predicate until it holds, or \p deadline passes.
//! \deprecated A poll can miss a state the gripper only passes through and
//! wakes up to a poll period late. Use waitFor(const Gripper&, Predicate,
//! std::chrono::milliseconds), which sees each exchange as it completes.
//! Removed in the next major release.
//!
//! \p predicate is evaluated at least once, even past the deadline: an
//! already-true condition never reports a timeout.
//! \tparam Predicate A callable taking no arguments, returning bool.
//! \param predicate The condition to wait for.
//! \param platform Supplies the sleep between polls.
//! \param deadline The time point past which waiting gives up.
//! \param pollPeriod How long to sleep between polls.
//! \return true if \p predicate held before \p deadline; false on timeout.
template <typename Predicate>
[[deprecated("use waitFor(gripper, predicate, timeout)")]] bool waitUntil(
   Predicate predicate,
   Platform& platform,
   std::chrono::steady_clock::time_point deadline,
   std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return detail::pollUntil(std::move(predicate), platform, deadline, pollPeriod);
}

//! \ingroup wait
//! \brief Poll \p predicate until it holds, or \p timeout elapses.
//! \deprecated Use waitFor(const Gripper&, Predicate, std::chrono::milliseconds).
//! Removed in the next major release.
//! \tparam Predicate A callable taking no arguments, returning bool.
//! \param predicate The condition to wait for.
//! \param platform Supplies the sleep between polls.
//! \param timeout How long to wait, starting now.
//! \param pollPeriod How long to sleep between polls.
//! \return true if \p predicate held within \p timeout; false on timeout.
template <typename Predicate>
[[deprecated("use waitFor(gripper, predicate, timeout)")]] bool waitFor(
   Predicate predicate,
   Platform& platform,
   std::chrono::milliseconds timeout,
   std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return detail::pollUntil(std::move(predicate), platform, std::chrono::steady_clock::now() + timeout, pollPeriod);
}

#if GRIPPERS_HOSTED
//! \ingroup wait
//! \overload
//! \deprecated Use waitFor(const Gripper&, Predicate, std::chrono::milliseconds).
//! Removed in the next major release.
//! Sleeps on the default (std::thread-backed) platform. Hosted-only.
template <typename Predicate>
[[deprecated("use waitFor(gripper, predicate, timeout)")]] bool waitUntil(
   Predicate predicate,
   std::chrono::steady_clock::time_point deadline,
   std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return detail::pollUntil(std::move(predicate), *makeDefaultPlatform(), deadline, pollPeriod);
}

//! \ingroup wait
//! \overload
//! \deprecated Use waitFor(const Gripper&, Predicate, std::chrono::milliseconds).
//! Removed in the next major release.
//! Sleeps on the default (std::thread-backed) platform. Hosted-only.
template <typename Predicate>
[[deprecated("use waitFor(gripper, predicate, timeout)")]] bool waitFor(
   Predicate predicate,
   std::chrono::milliseconds timeout,
   std::chrono::milliseconds pollPeriod = std::chrono::milliseconds(2))
{
   return detail::pollUntil(std::move(predicate),
                            *makeDefaultPlatform(),
                            std::chrono::steady_clock::now() + timeout,
                            pollPeriod);
}
#endif // GRIPPERS_HOSTED

} // namespace Robotiq
