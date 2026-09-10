target_sources(grippers_objects PRIVATE
  gripper.hpp
  gripper/command.hpp
  gripper/status.hpp
  gripper/fault_status.hpp
  gripper/wait.hpp
  gripper/connection_config.hpp
  gripper/connection_state.hpp
  gripper/driver_exception.hpp
  gripper/logger.hpp
  gripper/stderr_logger.hpp
  gripper/to_string.hpp
  gripper/device_profile.hpp
  gripper/units.hpp
  gripper/platform.hpp
  gripper/serial.hpp
  gripper/serial_config.hpp
  gripper/serial_io_exception.hpp
  gripper/modbus_client.hpp
  detail/config.hpp
  detail/register_map.hpp
  detail/named_bit_array.hpp
  detail/modbus_constants.hpp
  detail/byte_packing.hpp
  detail/throttle.hpp
  detail/default_logger.hpp
  detail/default_serial.hpp
  # Shims for the two v1.0.0 paths the README named; removed in the next major.
  gripper/register_map.hpp
  detail/gripper_modbus_client.hpp
)

if(GRIPPERS_BUILD_FAKE)
  target_sources(grippers_objects PRIVATE
    gripper/fake/gripper_factory.hpp
  )
endif()
