// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// Measures the exchange loop's achieved pacing against its configured rate,
// for verifying platform sleep granularity (robotiq/grippers#24).
//
//   exchange_rate_probe --port <port> [--hz 100] [--seconds 5]
//                       [--baudrate 115200]
//
// The baud rate must match the gripper's persisted setting (default 115200);
// too low a rate is itself an exchange-rate ceiling.
//
// Prints the platform's raw sleep-overshoot first (no hardware involved),
// then the distribution of exchange intervals measured on the wire.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <Robotiq/detail/default_serial.hpp>
#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/connection_config.hpp>
#include <Robotiq/gripper/platform.hpp>

using SteadyClock = std::chrono::steady_clock;

namespace {
double toMs(SteadyClock::duration d)
{
   return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(d).count();
}

// Timestamps every pacing sleep of the exchange thread; each entry is one
// completed exchange cycle.
class TimestampingPlatform final : public Robotiq::Platform
{
public:
   std::unique_ptr<Robotiq::Mutex> makeMutex() override { return _real->makeMutex(); }
   std::unique_ptr<Robotiq::Thread> spawn(std::function<void()> fn) override { return _real->spawn(std::move(fn)); }
   void sleepFor(std::chrono::milliseconds duration) override { _real->sleepFor(duration); }

   void sleepUntil(SteadyClock::time_point timePoint) override
   {
      {
         const std::lock_guard<std::mutex> lock(_mutex);
         _stamps.push_back(SteadyClock::now());
      }
      _real->sleepUntil(timePoint);
   }

   std::vector<SteadyClock::time_point> stamps()
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      return _stamps;
   }

private:
   std::shared_ptr<Robotiq::Platform> _real = Robotiq::makeDefaultPlatform();
   std::mutex _mutex;
   std::vector<SteadyClock::time_point> _stamps;
};

void printDistribution(const char* label, std::vector<double> ms)
{
   std::sort(ms.begin(), ms.end());
   const auto at = [&](double q) { return ms[static_cast<size_t>(q * (ms.size() - 1))]; };
   std::printf("%s over %zu samples: median %.2f ms, p90 %.2f ms, p99 %.2f ms, range %.2f-%.2f ms\n",
               label,
               ms.size(),
               at(0.5),
               at(0.9),
               at(0.99),
               ms.front(),
               ms.back());
}

// The tick shows up here as ~15.6 ms overshoots of a 3 ms sleep — measured
// before any serial traffic so the two ceilings can be told apart.
void probeSleepGranularity(Robotiq::Platform& platform)
{
   std::vector<double> overshoots;
   for(int i = 0; i < 50; ++i)
   {
      const auto deadline = SteadyClock::now() + std::chrono::milliseconds(3);
      platform.sleepUntil(deadline);
      overshoots.push_back(toMs(SteadyClock::now() - deadline));
   }
   std::printf("In the line below, a median over ~1 ms points to an OS timer-tick problem\n"
               "(no serial traffic is involved yet, so the FTDI latency timer is not a suspect):\n");
   printDistribution("sleepUntil(3 ms) overshoot", std::move(overshoots));
}

void usage(const char* prog)
{
   std::fprintf(stderr, "usage: %s --port <port> [--hz 100] [--seconds 5] [--baudrate 115200]\n", prog);
}

// False on anything but a whole number within [min, max].
bool parseLong(const char* text, long min, long max, long& out)
{
   char* end = nullptr;
   const long value = std::strtol(text, &end, 10);
   if(end == text || *end != '\0' || value < min || value > max)
   {
      return false;
   }
   out = value;
   return true;
}

// False on anything but a finite number within [min, max].
bool parseDouble(const char* text, double min, double max, double& out)
{
   char* end = nullptr;
   const double value = std::strtod(text, &end);
   if(end == text || *end != '\0' || !(value >= min && value <= max))
   {
      return false;
   }
   out = value;
   return true;
}

struct Options
{
   const char* port = nullptr; // null when the command line was rejected
   double hz = 100.0;
   long seconds = 5;
   long baudrate = 115200;
};

// Reads "--name value" pairs in any order; --port is required. A port left
// null means the arguments were rejected, and usage has been printed.
Options parseOptions(int argc, char** argv)
{
   Options options;
   // An even argc leaves a name without its value.
   bool ok = argc % 2 == 1;
   for(int i = 1; ok && i + 1 < argc; i += 2)
   {
      const char* name = argv[i];
      const char* value = argv[i + 1];
      if(std::strcmp(name, "--port") == 0)
      {
         options.port = value;
      }
      else if(std::strcmp(name, "--hz") == 0)
      {
         ok = parseDouble(value, 0.001, 1e6, options.hz);
      }
      else if(std::strcmp(name, "--seconds") == 0)
      {
         ok = parseLong(value, 1, 3600, options.seconds);
      }
      else if(std::strcmp(name, "--baudrate") == 0)
      {
         ok = parseLong(value, 1200, 3000000, options.baudrate);
      }
      else
      {
         ok = false;
      }
   }
   if(!ok || options.port == nullptr)
   {
      usage(argv[0]);
      options.port = nullptr;
   }
   return options;
}
} // namespace

int main(int argc, char** argv)
{
   const Options options = parseOptions(argc, argv);
   if(options.port == nullptr)
   {
      return 2;
   }

   probeSleepGranularity(*Robotiq::makeDefaultPlatform());

   try
   {
      const auto platform = std::make_shared<TimestampingPlatform>();
      const auto period = std::chrono::microseconds(static_cast<long long>(1e6 / options.hz));
      Robotiq::SerialConfig serialConfig;
      serialConfig.port = options.port;
      serialConfig.baudrate = static_cast<uint32_t>(options.baudrate);

      const Robotiq::Gripper gripper(std::make_unique<Robotiq::detail::DefaultSerial>(serialConfig),
                                     Robotiq::kDefaultModbusSlaveAddress,
                                     period,
                                     platform);
      platform->sleepFor(std::chrono::seconds(options.seconds));

      const auto stamps = platform->stamps();
      if(stamps.size() < 2)
      {
         std::fprintf(stderr, "no exchange cycles recorded\n");
         return 1;
      }
      std::vector<double> intervals;
      for(size_t i = 1; i < stamps.size(); ++i)
      {
         intervals.push_back(toMs(stamps[i] - stamps[i - 1]));
      }
      const double achievedHz = 1000.0 * intervals.size() / toMs(stamps.back() - stamps.front());
      std::printf("Below, intervals well over the configured period despite a clean sleep\n"
                  "overshoot above point to the FTDI latency timer (16 ms at its default):\n");
      std::printf("configured %.1f Hz (%.2f ms) at %ld baud, achieved %.1f Hz\n",
                  options.hz,
                  toMs(period),
                  options.baudrate,
                  achievedHz);
      printDistribution("exchange interval", std::move(intervals));
      return 0;
   }
   catch(const std::exception& ex)
   {
      std::fprintf(stderr, "%s\n", ex.what());
      return 1;
   }
}
