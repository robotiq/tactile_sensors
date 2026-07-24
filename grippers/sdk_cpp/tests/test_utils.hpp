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

#include <Robotiq/detail/serial.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/serial_io_exception.hpp>

namespace Robotiq::test {
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

//! Serial test double: captures everything written and serves reads from a
//! preloaded byte queue, in whatever chunk sizes the caller asks for.
//! An exhausted queue throws SerialIOException, like a real read timeout.
class ScriptedSerial : public detail::Serial
{
public:
   void open() override { _open = true; }
   [[nodiscard]] bool isOpen() const override { return _open; }
   void close() override { _open = false; }

   [[nodiscard]] std::vector<uint8_t> read(size_t size, std::chrono::milliseconds timeout) override
   {
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
   void clearWritten() { _written.clear(); }

private:
   bool _open = false;
   std::deque<uint8_t> _toRead;
   std::vector<uint8_t> _written;
};
} // namespace Robotiq::test
