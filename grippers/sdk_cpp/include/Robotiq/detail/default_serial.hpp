// Copyright (c) 2023 PickNik, Inc.
// Copyright (c) 2026 Robotiq, Inc. (libserialport rewrite)
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Serial implementation backed by libserialport (cross-platform:
//!        Linux, Windows, macOS).
//! Configures 8N1 with no flow control — the wire format of the Robotiq
//! gripper's Modbus RTU link. Link parameters are fixed at construction
//! (SerialConfig).
//! On Linux, open() additionally enforces the FTDI `latency_timer` via
//! sysfs. The kernel default of 16 ms silently triples Modbus cycle
//! latency; 1 ms restores it. Failure to apply it (e.g. missing
//! permissions, non-FTDI adapter) logs a warning and continues.

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <Robotiq/gripper/serial.hpp>
#include <Robotiq/gripper/serial_config.hpp>

struct sp_port; // opaque port handle from libserialport

namespace Robotiq {
class Logger;
} // namespace Robotiq

namespace Robotiq::detail {
class DefaultSerial : public Serial
{
public:
   // \param logger Log sink; pass null to use the default stderr logger.
   explicit DefaultSerial(SerialConfig config, std::shared_ptr<Logger> logger = nullptr);
   ~DefaultSerial() override;

   DefaultSerial(const DefaultSerial&) = delete;
   DefaultSerial& operator=(const DefaultSerial&) = delete;

   void open() override;

   [[nodiscard]] bool isOpen() const override;
   void close() override;

   [[nodiscard]] std::vector<uint8_t> read(size_t size, std::chrono::milliseconds timeout) override;
   void write(const std::vector<uint8_t>& data) override;

   [[nodiscard]] std::chrono::milliseconds getTimeout() const override;

   // Link parameters this connection was constructed with.
   [[nodiscard]] const SerialConfig& getConfig() const;

private:
   // Best-effort sysfs write; returns false when it could not be applied.
   [[nodiscard]] bool applyLatencyTimer() const;

   struct sp_port* _portHandle = nullptr;
   SerialConfig _config;
   std::shared_ptr<Logger> _logger;
};

// Strip the directory part of a device path ("/dev/ttyUSB0" -> "ttyUSB0");
// the sysfs latency_timer path is derived from it.
[[nodiscard]] std::string deviceBasename(const std::string& port);

} // namespace Robotiq::detail
