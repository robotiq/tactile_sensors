// Copyright (c) 2023 PickNik, Inc.
// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Serial implementation backed by the operating system's own
//!        serial API — termios on Linux and macOS, the Win32 comm API on
//!        Windows.
//! Configures 8N1 with no flow control — the wire format of the Robotiq
//! gripper's Modbus RTU link. Link parameters are fixed at construction
//! (SerialConfig).
//! On Linux, open() additionally enforces the FTDI `latency_timer` via
//! sysfs. The kernel default of 16 ms silently triples Modbus cycle
//! latency; 1 ms restores it. Ports with no FTDI adapter behind them
//! (ptys, other USB chips) are left alone; failure to apply it on an FTDI
//! adapter (e.g. missing permissions) logs a warning and continues.

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <Robotiq/gripper/serial.hpp>
#include <Robotiq/gripper/serial_config.hpp>

namespace Robotiq {
class Logger;
} // namespace Robotiq

namespace Robotiq::detail {
class SerialPort;

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
   [[nodiscard]] bool applyLatencyTimer(const std::string& path) const;

   // Null when closed; the OS handle lives inside it.
   std::unique_ptr<SerialPort> _port;
   SerialConfig _config;
   std::shared_ptr<Logger> _logger;
};

// Symlinks (/dev/serial/by-id, socat links) are followed to the device
// node, whose name is the sysfs entry. Empty when the port is not an FTDI
// adapter.
[[nodiscard]] std::string latencyTimerPath(const std::string& port,
                                           const std::string& sysfsDevices = "/sys/bus/usb-serial/devices");

} // namespace Robotiq::detail
