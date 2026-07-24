target_sources(grippers_sdk PRIVATE
  gripper/command.hpp
  gripper/named_bit_array.hpp
  gripper/status.hpp
  gripper/register_map.hpp
  gripper/fault_status.hpp
  gripper/connection_config.hpp
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
