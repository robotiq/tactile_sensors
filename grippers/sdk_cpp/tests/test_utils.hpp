// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Robotiq/gripper/serial.hpp>
#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/serial_io_exception.hpp>

namespace Robotiq::test {

//! The gripper's slave address, from the single source of truth.
constexpr uint8_t kSlaveAddress = kDefaultModbusSlaveAddress;

//! Captures every delivered line.
class CollectingLogger : public Logger
{
public:
   void log(Level level, std::string_view message) override { lines.emplace_back(level, std::string(message)); }

   [[nodiscard]] bool contains(std::string_view value) const
   {
      return std::any_of(lines.begin(), lines.end(), [&](const auto& line) {
         return line.second.find(value) != std::string::npos;
      });
   }

   std::vector<std::pair<Level, std::string>> lines;
};

//! Reference CRC-16/MODBUS (poly 0xA001 reflected). KAT: "123456789" -> 0x4B37.
inline uint16_t crc16Modbus(const std::vector<uint8_t>& data)
{
   uint16_t crc = 0xFFFF;
   for(const uint8_t byte : data)
   {
      crc ^= byte;
      for(int i = 0; i < 8; ++i)
      {
         crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xA001 : crc >> 1;
      }
   }
   return crc;
}

//! Append CRC-16/MODBUS to a frame, LSB first (Modbus RTU wire order).
inline std::vector<uint8_t> withCrc(std::vector<uint8_t> frame)
{
   const uint16_t crc = crc16Modbus(frame);
   frame.push_back(static_cast<uint8_t>(crc & 0xFF));
   frame.push_back(static_cast<uint8_t>(crc >> 8));
   return frame;
}

//! An FC 0x03 read-holding-registers request, CRC included. Address and
//! quantity are 16-bit fields, MSB first as Modbus puts them on the wire.
inline std::vector<uint8_t> readHoldingRegistersFrame(uint8_t slaveAddress, uint16_t address, uint16_t quantity)
{
   return withCrc({slaveAddress,
                   0x03,
                   static_cast<uint8_t>(address >> 8),
                   static_cast<uint8_t>(address & 0xFF),
                   static_cast<uint8_t>(quantity >> 8),
                   static_cast<uint8_t>(quantity & 0xFF)});
}

//! Serial test double: captures everything written and serves reads from a
//! preloaded byte queue, in whatever chunk sizes the caller asks for.
//! An exhausted queue reads empty, which is what a real timeout looks like
//! to nanomodbus.
class ScriptedSerial : public Serial
{
public:
   void open() override { _open = true; }
   [[nodiscard]] bool isOpen() const override { return _open; }
   void close() override { _open = false; }

   [[nodiscard]] std::vector<uint8_t> read(size_t size, std::chrono::milliseconds timeout) override
   {
      _readTimeouts.push_back(timeout);
      if(timeout.count() == 0)
      {
         return {}; // a drain sees no stale bytes; preloaded data is the future response
      }
      const auto count = std::min(size, _toRead.size());
      std::vector<uint8_t> data(_toRead.begin(), _toRead.begin() + static_cast<long>(count));
      _toRead.erase(_toRead.begin(), _toRead.begin() + static_cast<long>(count));
      return data;
   }

   void write(const std::vector<uint8_t>& data) override { _written.insert(_written.end(), data.begin(), data.end()); }

   [[nodiscard]] std::chrono::milliseconds getTimeout() const override { return std::chrono::milliseconds{100}; }

   void preloadRead(const std::vector<uint8_t>& data) { _toRead.insert(_toRead.end(), data.begin(), data.end()); }
   [[nodiscard]] const std::vector<uint8_t>& written() const { return _written; }

   // The timeout handed to each read(), in call order.
   [[nodiscard]] const std::vector<std::chrono::milliseconds>& readTimeouts() const { return _readTimeouts; }

private:
   bool _open = false;
   std::deque<uint8_t> _toRead;
   std::vector<uint8_t> _written;
   std::vector<std::chrono::milliseconds> _readTimeouts;
};

//! Serial test double whose transfers always fail, for the paths that have
//! to surface a wire-level cause rather than swallow it.
class ThrowingSerial : public Serial
{
public:
   static constexpr std::string_view kFailure = "the wire is on fire";

   void open() override { _open = true; }
   [[nodiscard]] bool isOpen() const override { return _open; }
   void close() override { _open = false; }

   [[nodiscard]] std::vector<uint8_t> read(size_t, std::chrono::milliseconds) override
   {
      throw SerialIOException(std::string(kFailure));
   }

   void write(const std::vector<uint8_t>&) override { throw SerialIOException(std::string(kFailure)); }

   [[nodiscard]] std::chrono::milliseconds getTimeout() const override { return std::chrono::milliseconds{100}; }

private:
   bool _open = false;
};
} // namespace Robotiq::test
