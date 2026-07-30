target_sources(grippers_sdk PRIVATE
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
  gripper/throttle.hpp
  gripper/serial_config.hpp
  gripper/serial_io_exception.hpp
  detail/modbus_constants.hpp
  detail/byte_packing.hpp
  detail/gripper_modbus_client.hpp
  detail/serial.hpp
  detail/default_serial.hpp
)

if(GRIPPERS_BUILD_FAKE)
  target_sources(grippers_sdk PRIVATE
    gripper/fake/gripper_factory.hpp
  )
endif()
