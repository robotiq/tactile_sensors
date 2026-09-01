target_sources(grippers_objects PRIVATE
  gripper.hpp
  gripper/command.hpp
  gripper/named_bit_array.hpp
  gripper/status.hpp
  gripper/register_map.hpp
  gripper/fault_status.hpp
  gripper/wait.hpp
  gripper/connection_config.hpp
  gripper/connection_state.hpp
  gripper/driver_exception.hpp
  gripper/logger.hpp
  gripper/stderr_logger.hpp
  gripper/throttle.hpp
  gripper/to_string.hpp
  gripper/platform.hpp
  gripper/serial.hpp
  gripper/serial_config.hpp
  gripper/serial_io_exception.hpp
  detail/config.hpp
  detail/modbus_constants.hpp
  detail/byte_packing.hpp
  detail/gripper_modbus_client.hpp
  detail/default_serial.hpp
)

if(GRIPPERS_BUILD_FAKE)
  target_sources(grippers_objects PRIVATE
    gripper/fake/gripper_factory.hpp
  )
endif()
