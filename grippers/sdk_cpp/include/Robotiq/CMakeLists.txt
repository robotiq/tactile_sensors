target_sources(grippers_sdk PRIVATE
  driver_exception.hpp
  gripper/command.hpp
  gripper/named_bit_array.hpp
  gripper/status.hpp
  gripper/register_map.hpp
  gripper/fault_status.hpp
  gripper/logger.hpp
  gripper/throttle.hpp
  gripper/serial_config.hpp
  gripper/serial_io_exception.hpp
  detail/serial.hpp
  detail/default_serial.hpp
)
