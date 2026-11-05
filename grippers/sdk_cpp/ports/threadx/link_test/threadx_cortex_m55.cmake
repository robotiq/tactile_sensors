# ThreadX toolchain file for cortex-m55, to be copied into the ThreadX source's
# own cmake/ directory — it includes its sibling arm-none-eabi.cmake from there.
#
# Upstream ships ports/cortex_m55/gnu but no cmake/cortex_m55.cmake to drive it,
# so the SDK carries one. This is upstream's cortex_m7.cmake with the CPU
# changed. Only the link test uses it; firmware brings its own build system.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m55)

set(THREADX_ARCH "cortex_m55")
set(THREADX_TOOLCHAIN "gnu")

set(MCPU_FLAGS "-mthumb -mcpu=cortex-m55")
set(VFP_FLAGS "")
set(SPEC_FLAGS "--specs=nosys.specs")

include(${CMAKE_CURRENT_LIST_DIR}/arm-none-eabi.cmake)
