// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

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

//! \ingroup core_api
//! \brief The Robotiq gripper Class.
//!
//! Construction opens the link, reads the gripper — failing like a dead
//! serial link when it does not answer — and starts the exchange cycle;
//! destruction stops it. Control is instant: setCommand() and getStatus()
//! access an internal process image and never touch the bus. The control
//! image seeds from the gripper's own status echoes, so connecting never
//! disturbs a gripper that is already running.
//!
//! **Concurrency model:** accessors are thread-safe; the intended use is
//! one control thread writing commands while a background exchange
//! thread reads/writes the wire. Reads are whole snapshots and writes are
//! whole commands — no per-field accessors, deliberately: every
//! transmitted frame is a command the application composed, and two
//! fields never come from different exchange cycles.
class Gripper
{
public:
#if GRIPPERS_BUILD_DEFAULT_SERIAL
   //! \brief The common-case constructor: create a gripper object and
   //!  start communication over the built-in serial transport and the default
   //!  platform.
   //!
   //! \param config see ConnectionConfig.
   //! \param logger Log sink; pass null to use the default stderr logger.
   //! \throw SerialIOException when the port cannot be opened/configured.
   //! \throw DriverException when no gripper answers the initial read, or
   //!        when config.connectionFrequency is invalid.
   explicit Gripper(const ConnectionConfig& config, std::shared_ptr<Logger> logger = nullptr);
#endif

   //! \brief A general-purpose constructor for cases where the common-case
   //! constructor does not apply. Start communication over a caller-supplied
   //! transport and platform.
   //!
   //! \param serial The transport to exchange over; must not be null.
   //! \param slaveAddress The gripper's Modbus slave address.
   //! \param exchangePeriod Period of the background exchange cycle.
   //! \param platform Runtime services (thread, lock, sleep) for the
   //!        exchange to run on; must not be null.
   //! \param logger Log sink; pass null to use the build's default logger.
   //! \throw SerialIOException when the port cannot be opened/configured.
   //! \throw DriverException when no gripper answers the initial read, when
   //!        exchangePeriod is invalid, or when platform is null.
   Gripper(std::unique_ptr<Serial> serial,
           uint8_t slaveAddress,
           std::chrono::microseconds exchangePeriod,
           std::shared_ptr<Platform> platform,
           std::shared_ptr<Logger> logger = nullptr);

   //! Stops the exchange cycle and closes the link.
   ~Gripper();

   Gripper(const Gripper&) = delete;
   Gripper& operator=(const Gripper&) = delete;

   //! \brief Send a new command block on the next exchange cycle.
   //! \param command The whole command block to transmit; see GripperCommand.
   void setCommand(const GripperCommand& command);

   //! \return The last command block passed to setCommand() — or the
   //!         gripper's own echoed state, before the first call.
   [[nodiscard]] GripperCommand getCommand() const;

   //! \return A snapshot of the gripper's last received status block.
   [[nodiscard]] GripperStatus getStatus() const;

   // TODO: add an exchange-cycle sync primitive so a caller's control loop
   // can run in step with the background exchange without polling

   //! \return The current state of the background exchange; see ConnectionState.
   [[nodiscard]] ConnectionState connectionState() const;

   //! \brief Return the runtime Platform used by this gripper.
   //!
   //! For most applications the function below should never be used. In some
   //! cases, such as embedded applications, it is needed for platform-specific
   //! versions of free functions such as waitFor and waitUntil.
   [[nodiscard]] Platform& platform() const noexcept;

private:
   struct Impl; // hides the link, the exchange thread, and the image
   std::unique_ptr<Impl> _impl;
};

//! \ingroup activation
//! Result of the blocking activation procedures activate() and recoverFromFault().
enum class ActivationResult
{
   Activated, //!< the gripper reports activation complete — the handshake
              //!< ran, or one already under way finished
   AlreadyActive, //!< already activated and fault-free; nothing was sent
   FaultLatched, //!< a major fault is latched; activate() refuses the reset
   Timeout, //!< the link stayed down, completion never arrived in time, or
            //!< too little of the timeout remained to run the handshake
};

//! \ingroup activation
//! \brief Ensure the gripper is activated, blocking until it reports completion.
//!
//! A healthy, already-activated gripper is left undisturbed, and an
//! activation already in progress is waited on rather than restarted.
//! rGTO is cleared when the handshake runs.
//! \param gripper The gripper to activate.
//! \param timeout How long to wait for the handshake to complete.
//! \return ActivationResult
//!
//! \par Example
//! \snippet move_gripper.cpp activation-recovery
ActivationResult activate(Gripper& gripper, std::chrono::milliseconds timeout = std::chrono::seconds(15));

//! \ingroup activation
//! \brief Run the manual's fault-recovery handshake, unconditionally.
//!
//! Clearing rACT resets the gripper — clearing its fault status — and
//! setting it back runs the calibration sweep.
//! \warning Releases any grip and moves the fingers through their full
//!          range. rGTO is cleared, as for activate().
//! \param gripper The gripper to recover.
//! \param timeout How long to wait for the handshake to complete.
//! \return Activated on success; Timeout if completion never arrived in time.
ActivationResult recoverFromFault(Gripper& gripper, std::chrono::milliseconds timeout = std::chrono::seconds(15));
} // namespace Robotiq
