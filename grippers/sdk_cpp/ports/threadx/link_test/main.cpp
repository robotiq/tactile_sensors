// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Link fixture for the freestanding ThreadX build. Instantiates the
//! whole threaded stack — Gripper on a ThreadXPlatform over an injected Serial —
//! so CI can link it for cortex-m55 against real ThreadX.
//!
//! Nothing here runs; the target has no runner. What the link proves is what a
//! compile cannot: that every symbol the SDK reaches for exists on a
//! freestanding multilib, including the exception machinery -fexceptions pulls
//! in and whatever backs std::chrono::steady_clock. It doubles as the smallest
//! complete example of wiring the SDK to an RTOS.

#include <memory>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/serial.hpp>
#include <threadx_platform.hpp>

namespace {
//! The integrator's transport. A real port drives a UART here — interrupt or
//! DMA, signalled through an RTOS semaphore so read() yields (see the port's
//! integration caveats). Empty bodies are enough to link.
class StubSerial : public Robotiq::Serial
{
public:
   void open() override {}
   [[nodiscard]] bool isOpen() const override { return true; }
   void close() override {}

   [[nodiscard]] std::vector<uint8_t> read(size_t, std::chrono::milliseconds) override { return {}; }
   void write(const std::vector<uint8_t>&) override {}

   [[nodiscard]] std::chrono::milliseconds getTimeout() const override { return std::chrono::milliseconds{100}; }
};
} // namespace

// Reached by nothing — the linker keeps it because main() calls it.
extern "C" int gripper_entry(void)
{
   auto platform = std::make_shared<Robotiq::ports::ThreadXPlatform>();
   Robotiq::Gripper gripper(std::make_unique<StubSerial>(), 9, std::chrono::microseconds{10000}, platform);

   // Pull the blocking helpers in too: they sleep on the platform, so this
   // covers the seam the RTOS port has to satisfy.
   const auto result = Robotiq::activate(gripper, std::chrono::seconds(2));
   gripper.setCommand(gripper.getCommand());
   return static_cast<int>(result);
}

extern "C" int main(void)
{
   return gripper_entry();
}
