target_sources(grippers_sdk PRIVATE
  gripper.cpp
  frequency.hpp
  exchange_period.hpp
  logger.cpp
  detail/gripper_modbus_client.cpp
  detail/default_serial.cpp
)

if(GRIPPERS_BUILD_FAKE)
  target_sources(grippers_sdk PRIVATE
    fake/frequency.hpp
    fake/gripper_factory.cpp
    fake/gripper_server.hpp
    fake/gripper_server.cpp
    fake/register_model.hpp
    fake/register_model.cpp
    fake/status_writer.hpp
    fake/status_writer.cpp
  )
endif()
