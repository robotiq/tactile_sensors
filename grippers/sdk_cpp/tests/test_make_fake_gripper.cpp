// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! Covers the shipped dummy through its public surface only — no reaching
//! into the fake device. These are the guarantees an integrator using
//! makeFakeGripper() for hardware-free bring-up is entitled to rely on, and
//! they are the behaviours the ROS wrapper's `use_dummy` mode inherits.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include <Robotiq/fake/gripper_factory.hpp>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/driver_exception.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/wait.hpp>

namespace Robotiq::test {
namespace {
constexpr auto kSettle = std::chrono::seconds{2};

std::unique_ptr<Gripper> makeQuietFakeGripper(ConnectionConfig config = {})
{
   return makeFakeGripper(config, std::make_shared<NullLogger>());
}
} // namespace

TEST(MakeFakeGripper, ConstructsWithNoPortAndComesUpOperational)
{
   // The whole point: no serial device exists, and nothing tries to open one.
   const auto gripper = makeQuietFakeGripper();
   ASSERT_NE(nullptr, gripper);
   EXPECT_EQ(ConnectionState::Operational, gripper->connectionState());
}

TEST(MakeFakeGripper, IgnoresSerialSettingsRatherThanFailingOnThem)
{
   // A caller flipping their real config over to the dummy leaves the port
   // and baud rate in place; they must not be honoured, or rejected.
   ConnectionConfig config;
   config.serial.port = "/dev/does-not-exist";
   config.serial.baudrate = 4321;
   EXPECT_NO_THROW({ const auto gripper = makeQuietFakeGripper(config); });
}

TEST(MakeFakeGripper, RefusesTheBroadcastAddressWithTheDocumentedException)
{
   // Address 0 is the Modbus broadcast address; no server can answer on it.
   // The factory's documented failure mode is DriverException — not the
   // internal error the Modbus layer reports.
   ConnectionConfig config;
   config.modbusSlaveAddress = 0;
   EXPECT_THROW({ const auto gripper = makeQuietFakeGripper(config); }, DriverException);
}

TEST(MakeFakeGripper, ActivatesAFreshGripperWithPlainActivate)
{
   // The first line an integrator writes: activate() on a gripper that has
   // just been built, with no fault to recover from first.
   const auto gripper = makeQuietFakeGripper();

   EXPECT_EQ(ActivationResult::Activated, activate(*gripper, kSettle));
   EXPECT_TRUE(gripper->getStatus().gripperStatus.activated());
   EXPECT_EQ(ActivationState::Complete, gripper->getStatus().gripperStatus.activationState());
}

TEST(MakeFakeGripper, HonoursANonDefaultSlaveAddress)
{
   ConnectionConfig config;
   config.modbusSlaveAddress = 0x25;

   const auto gripper = makeQuietFakeGripper(config);

   ASSERT_NE(nullptr, gripper);
   EXPECT_EQ(ActivationResult::Activated, activate(*gripper, kSettle));
   EXPECT_EQ(ConnectionState::Operational, gripper->connectionState());
}

TEST(MakeFakeGripper, ActivatesInstantly)
{
   const auto gripper = makeQuietFakeGripper();

   EXPECT_EQ(ActivationResult::Activated, recoverFromFault(*gripper, kSettle));
   EXPECT_TRUE(gripper->getStatus().gripperStatus.activated());
   EXPECT_EQ(ActivationState::Complete, gripper->getStatus().gripperStatus.activationState());
}

TEST(MakeFakeGripper, ActivateLeavesAnAlreadyActiveGripperAlone)
{
   const auto gripper = makeQuietFakeGripper();

   ASSERT_EQ(ActivationResult::Activated, recoverFromFault(*gripper, kSettle));
   EXPECT_EQ(ActivationResult::AlreadyActive, activate(*gripper, kSettle));
}

TEST(MakeFakeGripper, FingersAreWhereverTheyWereLastCommanded)
{
   // The dummy's defining behaviour, and the one the previous ROS driver's
   // fake had: position follows the request with no travel time.
   const auto gripper = makeQuietFakeGripper();
   ASSERT_EQ(ActivationResult::Activated, recoverFromFault(*gripper, kSettle));

   for(const uint8_t target : {uint8_t{0x00}, uint8_t{0x80}, uint8_t{0xFF}, uint8_t{0x2A}})
   {
      GripperCommand command = GripperCommand::defaults();
      command.action.set(ActionRequestBit::GoTo);
      command.positionRequest = target;
      gripper->setCommand(command);

      EXPECT_TRUE(waitFor([&] { return gripper->getStatus().position == target; }, kSettle))
         << "position never reached " << static_cast<int>(target) << "; stuck at "
         << static_cast<int>(gripper->getStatus().position);
   }
}

TEST(MakeFakeGripper, ReportsNoFault)
{
   const auto gripper = makeQuietFakeGripper();
   ASSERT_EQ(ActivationResult::Activated, recoverFromFault(*gripper, kSettle));

   EXPECT_EQ(GripperFault::None, gripper->getStatus().faultStatus.gripperFault());
   EXPECT_EQ(ControllerFault::None, gripper->getStatus().faultStatus.controllerFault());
}

TEST(MakeFakeGripper, ClearingTheCommandBlockDeactivates)
{
   const auto gripper = makeQuietFakeGripper();
   ASSERT_EQ(ActivationResult::Activated, recoverFromFault(*gripper, kSettle));

   gripper->setCommand(GripperCommand{});
   EXPECT_TRUE(waitFor([&] { return !gripper->getStatus().gripperStatus.activated(); }, kSettle));

   // ...and it can be brought back up afterwards.
   EXPECT_EQ(ActivationResult::Activated, recoverFromFault(*gripper, kSettle));
}

TEST(MakeFakeGripper, RejectsANegativeFrequency)
{
   ConnectionConfig config;
   config.connectionFrequency = -5.0;

   EXPECT_THROW({ const auto gripper = makeQuietFakeGripper(config); }, DriverException);
}

TEST(MakeFakeGripper, AnyRequestedFrequencyYieldsAWorkingGripper)
{
   // Requested rates are clamped; 0 is free-run, which for a fake with no
   // link to pace against is the top of the range.
   for(const double requested : {0.0, 20.0, 100.0, 1e9})
   {
      ConnectionConfig config;
      config.connectionFrequency = requested;

      const auto gripper = makeQuietFakeGripper(config);
      ASSERT_NE(nullptr, gripper) << "no gripper at " << requested << " Hz";
      EXPECT_EQ(ActivationResult::Activated, recoverFromFault(*gripper, kSettle))
         << "could not activate at " << requested << " Hz";
      EXPECT_EQ(ConnectionState::Operational, gripper->connectionState());
   }
}

TEST(MakeFakeGripper, EachGripperGetsItsOwnDevice)
{
   const auto first = makeQuietFakeGripper();
   const auto second = makeQuietFakeGripper();
   ASSERT_EQ(ActivationResult::Activated, recoverFromFault(*first, kSettle));
   ASSERT_EQ(ActivationResult::Activated, recoverFromFault(*second, kSettle));

   GripperCommand command = GripperCommand::defaults();
   command.action.set(ActionRequestBit::GoTo);
   command.positionRequest = 0xC0;
   first->setCommand(command);

   ASSERT_TRUE(waitFor([&] { return first->getStatus().position == 0xC0; }, kSettle));
   EXPECT_NE(0xC0, second->getStatus().position) << "the two dummies share a device";
}
} // namespace Robotiq::test
