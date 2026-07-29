// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Register model for a fake gripper.
//! Models the device's *protocol*: what the status block reports given what
//! the command block asked for. It knows nothing of Modbus, framing or bytes
//! — a transport reads and writes registers through it, and how those
//! register accesses arrive is not its concern.
//!
//! The behaviour is deliberately minimal: activation completes instantly and
//! the fingers arrive the moment they are commanded. Nothing physical is
//! modelled — no motion profile, no travel time, no object detection. Both
//! hooks below exist so richer behaviour, or instrumentation, can be layered
//! on by deriving rather than by editing this class.

#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <Robotiq/detail/modbus_constants.hpp>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/status.hpp>

namespace Robotiq::fake {

class RegisterModel
{
public:
   //! Registers per command/status block.
   static constexpr uint16_t kBlockRegisters = 8;

   //! gCU reported in every status block. A real gripper's motor current
   //! varies with load; a constant is enough to carry the field.
   static constexpr uint8_t kReportedCurrent = 0x2A;

   //! One contiguous span, address 0 through the end of the status block,
   //! matching the device's tolerant register map.
   static constexpr uint16_t kRegisterCount = detail::modbus_constants::kStatusAddress + kBlockRegisters;

   //! \param logger Sink for the one thing this class has to report — a
   //!        register access it had to refuse. Pass null for the default.
   explicit RegisterModel(std::shared_ptr<Logger> logger = nullptr);

   virtual ~RegisterModel() = default;

   //! Whether the register map covers [address, address + quantity).
   [[nodiscard]] static bool containsRange(uint16_t address, uint16_t quantity);

   //! Read holding registers into \p out. Command-block reads answer zeros
   //! and no error, as a real gripper does (bench-verified).
   virtual void read(uint16_t address, uint16_t quantity, uint16_t* out) const;

   //! Write holding registers, then let the device act on them.
   virtual void write(uint16_t address, uint16_t quantity, const uint16_t* values);

   //! Typed views of the two blocks, decoded from and encoded into the
   //! register file. Register framing is real only at this boundary — above
   //! it the device is written against the same GripperCommand and
   //! GripperStatus an application sees.
   [[nodiscard]] GripperCommand command() const;
   [[nodiscard]] GripperStatus status() const;
   void setStatus(const GripperStatus& status);

   //! Direct register access. The model *is* the register block, so this is
   //! its interface, not a back door: a transport reads and writes through
   //! read()/write(), while a caller staging a device state reaches here.
   [[nodiscard]] uint16_t& at(uint16_t address) { return _registers.at(address); }
   [[nodiscard]] uint16_t at(uint16_t address) const { return _registers.at(address); }

protected:
   [[nodiscard]] virtual GripperStatus processCommand(const GripperCommand& command,
                                                      const GripperStatus& currentStatus);

   std::shared_ptr<Logger> _logger;
   std::array<uint16_t, kRegisterCount> _registers{};

   //! rACT edge tracking. A falling edge resets; a rising edge starts the
   //! activation, which here completes instantly.
   bool _previousActivateBit = false;
   bool _activationDone = false;
};
} // namespace Robotiq::fake
