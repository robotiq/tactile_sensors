// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/driver_exception.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/serial_io_exception.hpp>
#include <Robotiq/detail/gripper_modbus_client.hpp>
#include <Robotiq/detail/modbus_constants.hpp>

#include "test_utils.hpp"

namespace Robotiq::test {

namespace {
//! Build a connection around a ScriptedSerial and hand back the raw pointer.
std::pair<std::unique_ptr<detail::GripperModbusClient>, ScriptedSerial*> makeClient()
{
   auto serial = std::make_unique<ScriptedSerial>();
   ScriptedSerial* raw = serial.get();
   auto connection =
      std::make_unique<detail::GripperModbusClient>(std::move(serial), kSlaveAddress, std::make_shared<NullLogger>());
   return {std::move(connection), raw};
}
} // namespace

TEST(TestCrc16Reference, known_answer_test)
{
   const std::vector<uint8_t> input = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
   EXPECT_EQ(crc16Modbus(input), 0x4B37);
}

TEST(TestGripperModbusClient, transfers_only_the_documented_registers)
{
   // The blocks are 16 bytes wide, but only three registers per block are
   // documented and travel on the wire.
   EXPECT_EQ(detail::modbus_constants::kCommandRegisterCount, 3);
   EXPECT_EQ(detail::modbus_constants::kStatusRegisterCount, 3);
}

TEST(TestGripperModbusClient, read_status_sends_canonical_fc03_frame)
{
   auto [client, serial] = makeClient();

   // Response: slave, FC 0x03, byte count 6, three registers, CRC.
   serial->preloadRead(withCrc({kSlaveAddress, 0x03, 0x06, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}));

   const auto status = client->readStatus();

   EXPECT_EQ(status.gripperStatus.raw(), 0x12);
   EXPECT_EQ(status.faultStatus.raw(), 0x56);
   EXPECT_EQ(status.positionRequestEcho, 0x78);
   EXPECT_EQ(status.position, 0x9A);
   EXPECT_EQ(status.current, 0xBC);
   // Request: slave, FC 0x03, address 0x07D0, count 3, CRC.
   EXPECT_EQ(serial->written(), withCrc({kSlaveAddress, 0x03, 0x07, 0xD0, 0x00, 0x03}));
}

TEST(TestGripperModbusClient, write_command_sends_canonical_fc10_frame)
{
   auto [client, serial] = makeClient();

   // Response: slave, FC 0x10, address, register count, CRC.
   serial->preloadRead(withCrc({kSlaveAddress, 0x10, 0x03, 0xE8, 0x00, 0x03}));

   GripperCommand command;
   std::memset(command.data(), 0, command.size());
   command.action.set(ActionRequestBit::Activate, true); // byte 0 = 0x01
   client->writeCommand(command);

   // Request: slave, FC 0x10, address 0x03E8, count 3, byte count 6, data, CRC.
   EXPECT_EQ(serial->written(),
             withCrc({kSlaveAddress, 0x10, 0x03, 0xE8, 0x00, 0x03, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}));
}

TEST(TestGripperModbusClient, exchange_sends_canonical_fc17_frame)
{
   auto [client, serial] = makeClient();

   // Response: slave, FC 0x17, byte count 6, three registers, CRC.
   serial->preloadRead(withCrc({kSlaveAddress, 0x17, 0x06, 0x31, 0x00, 0x00, 0x00, 0x03, 0x00}));

   GripperCommand command = GripperCommand::defaults();
   command.action.set(ActionRequestBit::GoTo, true);
   command.positionRequest = 0xFF;
   command.force = 0x80;
   const auto status = client->exchange(command);

   // Request: slave, FC 0x17, read 0x07D0 x3, write 0x03E8 x3, byte count 6, data, CRC.
   const std::vector<uint8_t> expectedRequest =
      {kSlaveAddress, 0x17, 0x07, 0xD0, 0x00, 0x03, 0x03, 0xE8, 0x00, 0x03, 0x06, 0x09, 0x00, 0x00, 0xFF, 0xFF, 0x80};
   EXPECT_EQ(serial->written(), withCrc(expectedRequest));
   EXPECT_EQ(status.gripperStatus.raw(), 0x31);
   EXPECT_EQ(status.position, 0x03);
}

// Every transaction the client can issue, so the failure paths below are
// checked against each function code rather than FC 0x03 alone. exchange()
// matters most: FC 0x17 applies the write before composing the read
// response, so a throw does not mean the write was skipped.
struct Transaction
{
   const char* name;
   uint8_t functionCode;
   std::vector<uint8_t> successfulResponse; // without CRC
   void (*invoke)(detail::GripperModbusClient&);
};

const std::vector<Transaction>& transactions()
{
   static const std::vector<Transaction> kTransactions = {
      {"readStatus",
       0x03,
       {kSlaveAddress, 0x03, 0x06, 0, 0, 0, 0, 0, 0},
       [](detail::GripperModbusClient& client) { (void)client.readStatus(); }},
      {"writeCommand",
       0x10,
       {kSlaveAddress, 0x10, 0x03, 0xE8, 0x00, 0x03},
       [](detail::GripperModbusClient& client) { client.writeCommand(GripperCommand::defaults()); }},
      {"exchange",
       0x17,
       {kSlaveAddress, 0x17, 0x06, 0, 0, 0, 0, 0, 0},
       [](detail::GripperModbusClient& client) { (void)client.exchange(GripperCommand::defaults()); }},
   };
   return kTransactions;
}

class TransactionFailureTest : public ::testing::TestWithParam<Transaction>
{
};

TEST_P(TransactionFailureTest, timeout_throws_driver_exception)
{
   auto [client, serial] = makeClient(); // no response preloaded: reads come up empty
   EXPECT_THROW(GetParam().invoke(*client), DriverException);
}

TEST_P(TransactionFailureTest, corrupted_crc_throws_driver_exception)
{
   auto [client, serial] = makeClient();

   auto response = withCrc(GetParam().successfulResponse);
   response.back() ^= 0xFF; // corrupt the CRC
   serial->preloadRead(response);

   EXPECT_THROW(GetParam().invoke(*client), DriverException);
}

TEST_P(TransactionFailureTest, modbus_exception_response_throws_driver_exception)
{
   auto [client, serial] = makeClient();

   // Exception response: FC | 0x80, code 0x04 (server device failure).
   serial->preloadRead(withCrc({kSlaveAddress, static_cast<uint8_t>(GetParam().functionCode | 0x80), 0x04}));

   EXPECT_THROW(GetParam().invoke(*client), DriverException);
}

INSTANTIATE_TEST_SUITE_P(EveryFunctionCode,
                         TransactionFailureTest,
                         ::testing::ValuesIn(transactions()),
                         [](const ::testing::TestParamInfo<Transaction>& info) { return info.param.name; });

TEST(TestGripperModbusClient, frame_delimiting_uses_a_short_byte_timeout)
{
   // The per-transaction timeout is the budget for the gripper to start
   // answering; the inter-byte gap inside a frame must be far shorter, or a
   // truncated frame stalls the caller for the whole transaction budget.
   auto [client, serial] = makeClient();
   serial->preloadRead(withCrc({kSlaveAddress, 0x03, 0x06, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}));

   (void)client->readStatus();

   // Zero-timeout reads are the drain of stale bytes before the request,
   // not a wait for the response.
   std::vector<std::chrono::milliseconds> waits;
   for(const auto timeout : serial->readTimeouts())
   {
      if(timeout.count() != 0)
      {
         waits.push_back(timeout);
      }
   }

   ASSERT_GE(waits.size(), 2U);
   EXPECT_EQ(waits.front(), serial->getTimeout()); // first byte: the transaction budget
   for(std::size_t i = 1; i < waits.size(); ++i)
   {
      EXPECT_LT(waits[i], serial->getTimeout()) << "in-frame read " << i << " waited the full transaction timeout";
   }
}

TEST(TestGripperModbusClient, serial_failure_is_logged_before_it_becomes_a_driver_exception)
{
   // The cause cannot cross the nanomodbus C boundary — it reaches the
   // caller as a generic transport error — so the logger carries it.
   auto logger = std::make_shared<CollectingLogger>();
   auto serial = std::make_unique<ThrowingSerial>();
   detail::GripperModbusClient client(std::move(serial), kSlaveAddress, logger);

   EXPECT_THROW((void)client.readStatus(), DriverException);
   EXPECT_TRUE(logger->contains(ThrowingSerial::kFailure));
}

TEST(TestGripperModbusClient, config_ctor_delivers_serial_logs_to_the_injected_logger)
{
   // Regression: the delegating constructor once moved the logger before
   // makeSerial read it (argument evaluation order is unspecified), so
   // the serial layer silently fell back to the default stderr logger.
   auto logger = std::make_shared<CollectingLogger>();
   ConnectionConfig config;
   config.serial.port = "/dev/this_should_not_exist";

   EXPECT_THROW(detail::GripperModbusClient(config, logger), SerialIOException);
   EXPECT_TRUE(logger->contains("opening serial port '/dev/this_should_not_exist'"));
}
} // namespace Robotiq::test
