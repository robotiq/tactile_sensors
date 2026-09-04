target_sources(grippers_objects PRIVATE
  gripper.cpp
  frequency.hpp
  exchange_period.hpp
  logger.cpp
  to_string.cpp
  detail/gripper_modbus_client.cpp
)

# TU selection does the platform work: the core above never mentions an OS,
# and these files supply the defaults (or their freestanding stand-ins).
if(GRIPPERS_HOSTED)
  target_sources(grippers_objects PRIVATE
    hosted/std_platform.cpp
    hosted/stderr_logger.cpp
  )
else()
  target_sources(grippers_objects PRIVATE
    freestanding/default_logger.cpp
  )
endif()

if(GRIPPERS_BUILD_FAKE)
  target_sources(grippers_objects PRIVATE
    fake/gripper_factory.cpp
    fake/gripper_serial.hpp
    fake/gripper_serial.cpp
    fake/gripper_server.hpp
    fake/gripper_server.cpp
    fake/register_model.hpp
    fake/register_model.cpp
    fake/status_writer.hpp
    fake/status_writer.cpp
  )
endif()

# libserialport-backed transport: desktop only (see GRIPPERS_BUILD_DEFAULT_SERIAL).
if(GRIPPERS_BUILD_DEFAULT_SERIAL)
  target_sources(grippers_objects PRIVATE
    default_serial_factory.cpp
    detail/default_serial.cpp
  )
endif()
