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

#include <Robotiq/detail/config.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/to_string.hpp>
#include <Robotiq/gripper/wait.hpp>

namespace Robotiq {
class Serial;

class Gripper
{
public:
#if GRIPPERS_BUILD_DEFAULT_SERIAL
   // \param logger Log sink; pass null to use the default stderr logger.
   // \throw SerialIOException when the port cannot be opened/configured;
   //        DriverException when no gripper answers the initial read, or when
   //        config.connectionFrequency is invalid.
   explicit Gripper(const ConnectionConfig& config, std::shared_ptr<Logger> logger = nullptr);
#endif

   // Constructor for custom serial implementations, unit tests, and RTOS
   // targets. The exchange runs on the given platform — makeDefaultPlatform()
   // on a hosted runtime, or your RTOS port (see Platform and ports/);
   // platform must not be null.
   // \param logger Log sink; pass null to use the build's default logger.
   // \throw as the ConnectionConfig overload; DriverException when platform
   //        is null.
   Gripper(std::unique_ptr<Serial> serial,
           uint8_t slaveAddress,
           std::chrono::microseconds exchangePeriod,
           std::shared_ptr<Platform> platform,
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

   [[nodiscard]] ConnectionState connectionState() const;

   // The Platform this gripper runs on — for composing blocking helpers
   // (as activate() does) that must sleep the way this gripper's target
   // sleeps.
   [[nodiscard]] Platform& platform() const noexcept;

private:
   struct Impl; // hides the link, the exchange thread, and the image
   std::unique_ptr<Impl> _impl;
};

//! Result of the blocking activation procedures.
enum class ActivationResult
{
   Activated, // the gripper reports activation complete — the handshake ran,
              // or one already under way finished
   AlreadyActive, // already activated and fault-free; nothing was sent
   FaultLatched, // a major fault is latched; activate() refuses the reset
   Timeout, // the link stayed down, completion never arrived in time, or too
            // little of the timeout remained to run the handshake
};

// Ensure the gripper is activated, blocking until it reports
// completion. A healthy, already-activated gripper is left undisturbed
// and an activation already in progress is waited on, not restarted.
[[nodiscard]] ActivationResult activate(Gripper& gripper, std::chrono::milliseconds timeout = std::chrono::seconds(15));

// The manual's fault-recovery handshake, run unconditionally: clearing
// rACT resets the gripper — clearing its fault status — and setting it
// back runs the calibration sweep. ⚠ Releases any grip and moves the
// fingers through their full range. rGTO is cleared, as for activate().
[[nodiscard]] ActivationResult recoverFromFault(Gripper& gripper,
                                                std::chrono::milliseconds timeout = std::chrono::seconds(15));
} // namespace Robotiq
