// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/fake/gripper_factory.hpp>

#include <memory>
#include <utility>

#include "exchange_period.hpp"
#include "fake/frequency.hpp"
#include "fake/register_model.hpp"
#include "fake/gripper_server.hpp"

namespace Robotiq {
namespace {
//! Ties the model and the server to the Serial handed to the Gripper, so all
//! three share a lifetime and the caller has nothing to keep alive.
class OwningFakeGripperSerial : public fake::GripperSerial
{
public:
   OwningFakeGripperSerial(std::unique_ptr<fake::RegisterModel> model, std::unique_ptr<fake::GripperServer> server)
      : GripperSerial(*server)
      , _model(std::move(model))
      , _server(std::move(server))
   {
   }

private:
   std::unique_ptr<fake::RegisterModel> _model;
   std::unique_ptr<fake::GripperServer> _server;
};
} // namespace

std::unique_ptr<Gripper> makeFakeGripper(const ConnectionConfig& config, std::shared_ptr<Logger> logger)
{
   auto model = std::make_unique<fake::RegisterModel>();
   auto server = std::make_unique<fake::GripperServer>(*model, config.modbusSlaveAddress);
   auto serial = std::make_unique<OwningFakeGripperSerial>(std::move(model), std::move(server));

   const double frequency = fake::clampFrequency(config.connectionFrequency);

   return std::make_unique<Gripper>(std::move(serial),
                                    config.modbusSlaveAddress,
                                    detail::exchangePeriodFromFrequency(frequency),
                                    std::move(logger));
}
} // namespace Robotiq
