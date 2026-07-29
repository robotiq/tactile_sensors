target_sources(grippers_sdk PRIVATE
  gripper.cpp
  frequency.hpp
  logger.cpp
  detail/gripper_modbus_client.cpp
  detail/default_serial.cpp
)
