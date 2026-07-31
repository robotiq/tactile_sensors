//! Calls an SDK entry point that consumer.cpp never references, resolving it from
//! the consumer library alone — this target does not link the SDK.
//!
//! Linking is the real assertion. With every object in the consumer library,
//! StderrLogger::log resolves; with only the referenced ones, as a plain static
//! archive would give, the link fails with an undefined reference. Running it then
//! confirms the code is callable and not merely present.

#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/stderr_logger.hpp>

extern "C" int grippers_consumer_present();

int main()
{
   if(grippers_consumer_present() != 1)
   {
      return 1;
   }

   Robotiq::StderrLogger logger;
   logger.log(Robotiq::Logger::Level::Info, "SDK entry point reached through the consumer library");
   return 0;
}
