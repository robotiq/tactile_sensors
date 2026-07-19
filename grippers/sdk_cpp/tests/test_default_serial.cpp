// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// Note: libserialport validates that a port is a real serial device, so
// PTY-based loopback tests are not possible with this backend (verified:
// sp_get_port_by_name rejects /dev/pts/* and symlinks to it). DefaultSerial's
// wire behavior is validated on hardware via examples/move_gripper.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include <Robotiq/detail/default_serial.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/serial_io_exception.hpp>

namespace Robotiq::test {
using detail::DefaultSerial;

namespace {
DefaultSerial makeSerial(SerialConfig config = {})
{
   return DefaultSerial(std::move(config), std::make_shared<NullLogger>());
}
} // namespace

TEST(TestDefaultSerial, throws_when_port_does_not_exist)
{
   SerialConfig config;
   config.port = "/dev/this_should_not_exist";
   auto serial = makeSerial(config);
   EXPECT_THROW(serial.open(), SerialIOException);
   EXPECT_FALSE(serial.isOpen());
}

TEST(TestDefaultSerial, throws_when_port_is_empty)
{
   auto serial = makeSerial();
   EXPECT_THROW(serial.open(), SerialIOException);
}

TEST(TestDefaultSerial, read_and_write_throw_on_closed_port)
{
   auto serial = makeSerial();
   EXPECT_THROW((void)serial.read(1, std::chrono::milliseconds{10}), SerialIOException);
   EXPECT_THROW(serial.write({0x01}), SerialIOException);
}

TEST(TestDefaultSerial, close_is_idempotent_on_a_never_opened_port)
{
   auto serial = makeSerial();
   EXPECT_NO_THROW(serial.close());
   EXPECT_NO_THROW(serial.close());
   EXPECT_FALSE(serial.isOpen());
}

TEST(TestDefaultSerial, construction_config_round_trip)
{
   SerialConfig config;
   config.port = "/dev/ttyUSB0";
   config.baudrate = 230400;
   config.timeout = std::chrono::milliseconds{200};
   config.latencyTimerMs = 1;
   auto serial = makeSerial(config);

   EXPECT_EQ(serial.getConfig().port, "/dev/ttyUSB0");
   EXPECT_EQ(serial.getConfig().baudrate, 230400U);
   EXPECT_EQ(serial.getConfig().timeout, std::chrono::milliseconds{200});
   EXPECT_EQ(serial.getConfig().latencyTimerMs, 1);
   EXPECT_EQ(serial.getTimeout(), std::chrono::milliseconds{200});
}
} // namespace Robotiq::test
