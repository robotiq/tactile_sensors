// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include <Robotiq/detail/default_serial.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/serial_io_exception.hpp>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>
#endif

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

#ifdef _WIN32
   config.port = "COM255";
#else
   config.port = "/dev/this_should_not_exist";
#endif
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

#ifndef _WIN32
#ifdef __linux__
class LatencyTimerPath : public ::testing::Test
{
protected:
   void SetUp() override
   {
      std::string base = ::testing::TempDir() + "latency_timer_XXXXXX";
      ASSERT_NE(::mkdtemp(base.data()), nullptr);
      _root = base;
      std::filesystem::create_directories(_root / "dev");
      std::filesystem::create_directories(_root / "sysfs/ttyUSB7");
      std::ofstream(_root / "dev/ttyUSB7").put('\0');
      std::ofstream(_root / "dev/ttyACM0").put('\0');
      std::ofstream(_root / "sysfs/ttyUSB7/latency_timer") << 16;
   }

   void TearDown() override { std::filesystem::remove_all(_root); }

   [[nodiscard]] std::string lookup(const std::filesystem::path& port) const
   {
      return detail::latencyTimerPath(port.string(), (_root / "sysfs").string());
   }

   std::filesystem::path _root;
};

TEST_F(LatencyTimerPath, FindsTheAttributeOfAnFtdiNode)
{
   EXPECT_EQ(lookup(_root / "dev/ttyUSB7"), (_root / "sysfs/ttyUSB7/latency_timer").string());
}

TEST_F(LatencyTimerPath, FollowsSymlinksToTheDeviceNode)
{
   std::filesystem::create_directories(_root / "dev/serial/by-id");
   std::filesystem::create_symlink(_root / "dev/ttyUSB7", _root / "dev/serial/by-id/usb-FTDI_X-if00-port0");

   EXPECT_EQ(lookup(_root / "dev/serial/by-id/usb-FTDI_X-if00-port0"),
             (_root / "sysfs/ttyUSB7/latency_timer").string());
}

TEST_F(LatencyTimerPath, IsEmptyForANodeWithoutTheAttribute)
{
   EXPECT_EQ(lookup(_root / "dev/ttyACM0"), "");
}

TEST_F(LatencyTimerPath, IsEmptyForAMissingPort)
{
   EXPECT_EQ(lookup(_root / "dev/ttyUSB9"), "");
}
#endif

using namespace std::chrono_literals;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace {
// A pseudo-terminal pair standing in for the gripper's serial link: the
// slave is a real tty, so DefaultSerial drives it through the same
// termios path as an FTDI adapter, and the master is the peer that
// answers.
class PtyLoopback
{
public:
   PtyLoopback()
   {
      _master = ::posix_openpt(O_RDWR | O_NOCTTY);
      if(_master < 0 || ::grantpt(_master) != 0 || ::unlockpt(_master) != 0)
      {
         return;
      }
      const char* name = ::ptsname(_master);
      if(name != nullptr)
      {
         _slavePath = name;
      }
   }

   ~PtyLoopback()
   {
      if(_master >= 0)
      {
         ::close(_master);
      }
   }

   PtyLoopback(const PtyLoopback&) = delete;
   PtyLoopback& operator=(const PtyLoopback&) = delete;

   [[nodiscard]] bool valid() const { return _master >= 0 && !_slavePath.empty(); }
   [[nodiscard]] const std::string& slavePath() const { return _slavePath; }

   // An open DefaultSerial on the slave end.
   [[nodiscard]] std::unique_ptr<DefaultSerial> openSerial(
      std::chrono::milliseconds timeout = SerialConfig{}.timeout) const
   {
      SerialConfig config;
      config.port = _slavePath;
      config.timeout = timeout;
      auto serial = std::make_unique<DefaultSerial>(std::move(config), std::make_shared<NullLogger>());
      serial->open();
      return serial;
   }

   void hangUp()
   {
      ::close(_master);
      _master = -1;
   }

   void send(const std::vector<uint8_t>& data) const
   {
      EXPECT_EQ(::write(_master, data.data(), data.size()), static_cast<ssize_t>(data.size()));
   }

   // Up to size bytes from the peer's end. The deadline is a guard
   // against hanging a failing test, not a behaviour under test.
   [[nodiscard]] std::vector<uint8_t> receive(size_t size) const
   {
      std::vector<uint8_t> data(size);
      size_t received = 0;
      const auto deadline = std::chrono::steady_clock::now() + 2s;
      while(received < size && std::chrono::steady_clock::now() < deadline)
      {
         struct pollfd descriptor = {};
         descriptor.fd = _master;
         descriptor.events = POLLIN;
         if(::poll(&descriptor, 1, 100) <= 0)
         {
            continue;
         }
         const ssize_t count = ::read(_master, data.data() + received, size - received);
         if(count > 0)
         {
            received += static_cast<size_t>(count);
         }
         else if(count < 0 && errno != EAGAIN && errno != EINTR)
         {
            break;
         }
      }
      data.resize(received);
      return data;
   }

private:
   int _master = -1;
   std::string _slavePath;
};

class PtySerialTest : public ::testing::Test
{
protected:
   void SetUp() override
   {
      if(!_pty.valid())
      {
         GTEST_SKIP() << "no pseudo-terminal available on this machine";
      }
   }

   PtyLoopback _pty;
};
} // namespace

TEST_F(PtySerialTest, open_close_and_reopen_a_real_tty)
{
   const auto serial = _pty.openSerial();
   EXPECT_TRUE(serial->isOpen());

   serial->close();
   EXPECT_FALSE(serial->isOpen());

   EXPECT_NO_THROW(serial->open());
   EXPECT_TRUE(serial->isOpen());
}

TEST_F(PtySerialTest, opening_an_already_open_port_is_ignored)
{
   const auto serial = _pty.openSerial();
   EXPECT_NO_THROW(serial->open());
   EXPECT_TRUE(serial->isOpen());
}

TEST_F(PtySerialTest, write_reaches_the_peer)
{
   const std::vector<uint8_t> request = {0x09, 0x03, 0x07, 0xD0, 0x00, 0x03};
   const auto serial = _pty.openSerial();
   // write() waits for the output to drain, which on macOS a pty does only
   // once the peer has read it, so the peer must already be reading.
   auto received = std::async(std::launch::async, [&] { return _pty.receive(request.size()); });
   serial->write(request);

   EXPECT_EQ(received.get(), request);
}

#ifdef __APPLE__
// Only a macOS pty holds written bytes until the peer reads them.
TEST_F(PtySerialTest, write_times_out_when_the_peer_never_reads)
{
   const auto serial = _pty.openSerial(20ms);

   EXPECT_THROW(serial->write({0x09, 0x03, 0x07, 0xD0, 0x00, 0x03}), SerialIOException);
}
#endif

TEST_F(PtySerialTest, read_waits_for_the_requested_bytes)
{
   const std::vector<uint8_t> response = {0x09, 0x03, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00};
   const auto serial = _pty.openSerial();
   _pty.send(response);

   EXPECT_EQ(serial->read(response.size(), 500ms), response);
}

TEST_F(PtySerialTest, read_raises_when_the_peer_hangs_up)
{
   const auto serial = _pty.openSerial();
   _pty.hangUp();

   EXPECT_THROW((void)serial->read(1, 500ms), SerialIOException);
}

TEST_F(PtySerialTest, read_waits_for_bytes_that_have_not_arrived_yet)
{
   const auto serial = _pty.openSerial();

   // The tail of each frame is sent only once the read is under way, so
   // the read has to wait for it rather than settle for what was already
   // buffered. Repeated because the behaviour under test is what happens
   // at the moment the port runs dry mid-frame, and a single round could
   // miss it by having the peer get there first.
   for(int round = 0; round < 32; ++round)
   {
      _pty.send({0x09, 0x03});
      std::thread tail([this] { _pty.send({0x06, 0x11}); });
      const auto frame = serial->read(4, 2s);
      tail.join();

      ASSERT_THAT(frame, ElementsAre(0x09, 0x03, 0x06, 0x11));
   }
}

TEST_F(PtySerialTest, a_zero_timeout_read_takes_only_what_is_buffered)
{
   const auto serial = _pty.openSerial();
   _pty.send({0xAA, 0xBB, 0xCC});

   // The pty delivers the whole chunk to the slave's queue at once, so
   // this blocking byte also settles the two behind it.
   ASSERT_THAT(serial->read(1, 500ms), ElementsAre(0xAA));

   // Asks for far more than is buffered: it must return the rest
   // instead of waiting for the full count.
   EXPECT_THAT(serial->read(64, 0ms), ElementsAre(0xBB, 0xCC));
   EXPECT_THAT(serial->read(64, 0ms), IsEmpty());
}

TEST_F(PtySerialTest, open_rejects_a_baud_rate_the_platform_has_no_constant_for)
{
   SerialConfig config;
   config.port = _pty.slavePath();
   config.baudrate = 12345;
   config.latencyTimerMs = 0;
   auto serial = makeSerial(config);

   EXPECT_THROW(serial.open(), SerialIOException);
   EXPECT_FALSE(serial.isOpen());
}
#endif

} // namespace Robotiq::test
