// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "fake/gripper_serial.hpp"

#include "fake/gripper_server.hpp"

namespace Robotiq::fake {

GripperSerial::GripperSerial(GripperServer& gripperServer)
   : _gripperServer(gripperServer)
{
}

void GripperSerial::open()
{
   _open = true;
}

bool GripperSerial::isOpen() const
{
   return _open;
}

void GripperSerial::close()
{
   _open = false;
}

std::vector<uint8_t> GripperSerial::read(size_t size, std::chrono::milliseconds timeout)
{
   if(timeout.count() == 0)
   {
      return {}; // drains see no stale bytes; replies only exist after a request
   }
   return _gripperServer.drain(size);
}

void GripperSerial::write(const std::vector<uint8_t>& data)
{
   _gripperServer.deliver(data);
}

std::chrono::milliseconds GripperSerial::getTimeout() const
{
   return std::chrono::milliseconds{100};
}
} // namespace Robotiq::fake
