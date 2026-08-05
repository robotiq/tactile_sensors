// Copyright (c) 2023 PickNik, Inc.
// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <gmock/gmock.h>

#include <chrono>
#include <vector>

#include <Robotiq/gripper/serial.hpp>

namespace Robotiq::test {
class MockSerial : public Robotiq::Serial
{
public:
   MOCK_METHOD(void, open, (), (override));
   MOCK_METHOD(bool, isOpen, (), (override, const));
   MOCK_METHOD(void, close, (), (override));
   MOCK_METHOD(std::vector<uint8_t>, read, (size_t size, std::chrono::milliseconds timeout), (override));
   MOCK_METHOD(void, write, (const std::vector<uint8_t>& buffer), (override));
   MOCK_METHOD(std::chrono::milliseconds, getTimeout, (), (override, const));
};
} // namespace Robotiq::test
