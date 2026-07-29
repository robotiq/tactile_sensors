// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/fake/gripper_factory.hpp>

#include <memory>
#include <utility>

#include "exchange_period.hpp"
#include "frequency.hpp"
#include "fake/register_model.hpp"
#include "fake/gripper_serial.hpp"
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
   auto sink = logger ? std::move(logger) : makeDefaultLogger();

   const double requested = config.connectionFrequency;
   const double supported = detail::clampFrequency(requested);
   // Free-run means "as fast as the link allows"; with no link, that is the
   // ceiling. Left to the transport, since a real one is paced by the wire.
   const double frequency = supported == 0.0 ? detail::kMaxFrequency : supported;
   if(requested > 0.0 && frequency != requested)
   {
      sink->log(Logger::Level::Warn,
                "requested exchange frequency " + std::to_string(requested) + " Hz is outside the fake gripper's "
                   + std::to_string(detail::kMinFrequency) + " to " + std::to_string(detail::kMaxFrequency)
                   + " Hz range; using " + std::to_string(frequency) + " Hz");
   }

   auto model = std::make_unique<fake::RegisterModel>(sink);
   auto server = std::make_unique<fake::GripperServer>(*model, config.modbusSlaveAddress);
   auto serial = std::make_unique<OwningFakeGripperSerial>(std::move(model), std::move(server));

   return std::make_unique<Gripper>(std::move(serial),
                                    config.modbusSlaveAddress,
                                    detail::exchangePeriodFromFrequency(frequency),
                                    std::move(sink));
}
} // namespace Robotiq
