// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include <nanomodbus.h>

#include <Robotiq/detail/modbus_constants.hpp>
#include <Robotiq/detail/serial.hpp>

namespace Robotiq::test {

//! In-process fake gripper: a nanomodbus RTU *server* implementing the 2F
//! register map over in-memory byte queues. Exercises the real client
//! framing end-to-end: request encoding, CRC, response parsing.
class FakeGripperModbusServer
{
public:
   explicit FakeGripperModbusServer(uint8_t slaveAddress = 0x09);

   // Process whatever request bytes are pending in fromClientStream.
   void poll();

   // Pre-seed the state of a gripper that retained a completed
   // activation from a previous session.
   void givenGripperIsActivated();

   // Pre-seed a latched gripper fault (gFLT code).
   void givenGripperFault(uint8_t faultCode);

   static constexpr uint8_t kSimulatedCurrent = 0x2A;

   // Test knob: when set, the status byte is pinned to this value
   // regardless of the command (e.g. a gripper stuck mid-activation).
   std::optional<uint8_t> forcedStatusByte;

   bool previousActivateBit = false;
   bool activationDone = false;

   // Transaction counters, readable from the test thread while the
   // exchange thread drives the server. Each rACT falling edge is a
   // reset request, so `resets` distinguishes a skipped handshake from a real
   // one.
   std::atomic<int> statusReads{0};
   std::atomic<int> commandWrites{0};
   std::atomic<int> resets{0};

   // Flat register file indexed by absolute Modbus address, mirroring the
   // gripper's holding registers.
   static constexpr uint16_t kBlockRegisters = 8; // registers per block
   std::array<uint16_t, detail::modbus_constants::kStatusAddress + kBlockRegisters> registers{};
   std::deque<uint8_t> fromClientStream;
   std::deque<uint8_t> toClientStream;
   nmbs_t server{};

private:
   static int32_t readFromClient(uint8_t* buf, uint16_t count, int32_t timeout, void* arg);
   static int32_t writeToClient(const uint8_t* buf, uint16_t count, int32_t timeout, void* arg);
   static nmbs_error onReadHoldingRegisters(uint16_t address,
                                            uint16_t quantity,
                                            uint16_t* registersOut,
                                            uint8_t unitId,
                                            void* arg);
   static nmbs_error onWriteMultipleRegisters(uint16_t address,
                                              uint16_t quantity,
                                              const uint16_t* values,
                                              uint8_t unitId,
                                              void* arg);

   // Minimal gripper behavior: activation completes instantly and the
   // fingers move to the requested position.
   void simulate();
};

//! Serial implementation that pipes into the fake gripper synchronously:
//! each write delivers a request and runs the server; reads drain the
//! reply.
class FakeGripperSerial : public detail::Serial
{
public:
   explicit FakeGripperSerial(FakeGripperModbusServer& gripper);

   void open() override;
   [[nodiscard]] bool isOpen() const override;
   void close() override;

   [[nodiscard]] std::vector<uint8_t> read(size_t size, std::chrono::milliseconds timeout) override;
   void write(const std::vector<uint8_t>& data) override;

   [[nodiscard]] std::chrono::milliseconds getTimeout() const override;

protected:
   FakeGripperModbusServer& _gripper;

private:
   bool _open = false;
};
} // namespace Robotiq::test
