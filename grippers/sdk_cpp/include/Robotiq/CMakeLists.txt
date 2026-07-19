target_sources(grippers_sdk PRIVATE
  driver_exception.hpp
  gripper/logger.hpp
  gripper/throttle.hpp
  gripper/serial_config.hpp
  gripper/serial_io_exception.hpp
  detail/serial.hpp
  detail/default_serial.hpp
)
