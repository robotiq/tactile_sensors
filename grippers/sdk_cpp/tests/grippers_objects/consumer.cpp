//! Stands in for a downstream library that re-exports the SDK's headers as part
//! of its own API — a ROS 2 hardware plugin, for instance.
//!
//! It deliberately references nothing from the SDK. Linking
//! Robotiq::grippers_objects still has to put every SDK object into this shared
//! library, because a caller holding our headers may reach for any SDK entry
//! point. caller.cpp then calls one of those entry points through this library,
//! and would fail to link if the unreferenced objects had been dropped.

extern "C" int grippers_consumer_present()
{
   return 1;
}
