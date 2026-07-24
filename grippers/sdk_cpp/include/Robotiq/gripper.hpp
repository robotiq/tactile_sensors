// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The Robotiq gripper API.
//! Construction opens the link, reads the gripper — failing like a
//! dead serial link when it does not answer — and starts the exchange
//! cycle; destruction stops it. Control is instant: setCommand() and
//! getStatus() access an internal process image and never touch the
//! bus. The control image seeds from the gripper's status echoes.
//! Concurrency model: accessors are thread-safe; intended use is one
//! control thread writing commands.
//! Reads are whole snapshots and writes are whole commands — no
//! per-field accessors, deliberately: every transmitted frame is a
//! command the application composed, and two fields never come from
//! different exchange cycles.
//! Blocking procedures — activate(), recoverFromFault() — are free
//! functions composed over these accessors, not members: the class
//! itself never blocks.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/wait.hpp>

namespace Robotiq {
namespace detail {
class Serial;
} // namespace detail

class Gripper
{
public:
   // \param logger Log sink; pass null to use the default stderr logger.
   // \throw SerialIOException when the port cannot be opened/configured;
   //        DriverException when no gripper answers the initial read.
   explicit Gripper(const ConnectionConfig& config, std::shared_ptr<Logger> logger = nullptr);

   // Constructor for unit tests or custom serial implementations.
   // \throw as the ConnectionConfig overload.
   Gripper(std::unique_ptr<detail::Serial> serial,
           uint8_t slaveAddress,
           std::chrono::microseconds exchangePeriod,
           std::shared_ptr<Logger> logger = nullptr);

   // Stops the exchange cycle and closes the link.
   ~Gripper();

   Gripper(const Gripper&) = delete;
   Gripper& operator=(const Gripper&) = delete;

   void setCommand(const GripperCommand& command);
   [[nodiscard]] GripperCommand getCommand() const;
   [[nodiscard]] GripperStatus getStatus() const;

   // TODO: add an exchange-cycle sync primitive so a caller's control loop
   // can run in step with the background exchange without polling

   // TODO: add debug helpers rendering the command and status blocks in
   // a human-readable form (named fields, decoded bits and fault codes)

   [[nodiscard]] ConnectionState connectionState() const;

private:
   struct Impl; // hides the link, the exchange thread, and the image
   std::unique_ptr<Impl> _impl;
};

//! Result of the blocking activation procedures.
enum class ActivationResult
{
   Activated, // the activation handshake ran and reported completion
   AlreadyActive, // already activated and fault-free; nothing was sent
   FaultLatched, // a major fault is latched; activate() refuses the reset
   Timeout, // the link stayed down or completion never arrived in time
};

// Ensure the gripper is activated, blocking until it reports
// completion. A healthy, already-activated gripper is left undisturbed
// and an activation already in progress is waited on, not restarted.
// With a major fault latched this refuses and returns FaultLatched:
// the recovery reset releases any grip and sweeps the fingers, so
// running it is an application decision — see recoverFromFault().
[[nodiscard]] ActivationResult activate(Gripper& gripper, std::chrono::milliseconds timeout = std::chrono::seconds(15));

// The manual's fault-recovery handshake, run unconditionally: clearing
// rACT resets the gripper — clearing its fault status — and setting it
// back runs the calibration sweep. ⚠ Releases any grip and moves the
// fingers through their full range; the pending command is replaced
// with GripperCommand::defaults().
[[nodiscard]] ActivationResult recoverFromFault(Gripper& gripper,
                                                std::chrono::milliseconds timeout = std::chrono::seconds(15));
} // namespace Robotiq
