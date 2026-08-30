if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico CACHE STRING "Board type")
cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)

set(CARD_NAME "shepard")
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(${CARD_NAME}
  main.cpp
  shepard.cpp)

# -Wdouble-promotion and -Wfloat-conversion are not cosmetic here. The RP2040
# has no FPU, so a single float in the audio path costs ~360ns against a 20.8us
# per-sample budget. These flags are what catch one sneaking in at BUILD time
# rather than on hardware.
target_compile_options(${CARD_NAME} PRIVATE
  -Wdouble-promotion -Wfloat-conversion -Wall -Wextra -O2)

# Worth watching every build: a sudden jump means something large landed in the
# wrong section. The SPIRAL delay buffer is 128KB, so RAM should sit near 51%.
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# REQUIRED for the Workshop Computer's crystal. Without it the card fails to
# boot on a cold power-up but works from a warm reset, which is exactly what
# makes the bug so confusing. Also #defined in fixed.h - the upstream registry
# validator greps the source for that form and does not see this flag.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

# NOTE: PICO_INT64_OPS_IN_RAM is deliberately NOT set.
#
# WorkshopSpectral needs it because FFT butterflies genuinely require 64-bit
# products, forcing calls to __aeabi_lmul which lives in flash by default. This
# card's whole arithmetic strategy - MulQ15 and MulQ30 - exists to avoid 64-bit
# multiplies in the hot path entirely. If a build ever pulls __aeabi_lmul into
# the audio path, that is a BUG TO FIND, not a flag to add:
#
#     arm-none-eabi-nm build/shepard.elf | grep aeabi_lmul
#
# (Init-time and control-rate 64-bit maths is fine and expected.)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${CARD_NAME}
  pico_stdlib
  pico_multicore
  hardware_dma
  hardware_i2c
  hardware_pwm
  hardware_adc
  hardware_spi
  hardware_vreg
  hardware_flash)

pico_add_extra_outputs(${CARD_NAME})

# Copy the .uf2 to FLASHME/ on every successful build.
#
# This is not a convenience - it is a correctness fix. FLASHME/ was populated
# by hand once and then went stale, so it held a build from before two knob
# mappings were fixed. Flashing it would have given a card whose glide was 15x
# too slow and whose SPIRAL shift spanned a fortieth of its range, silently
# contradicting the documentation. A stale artefact is worse than no artefact,
# because it looks current.
#
# FLASHME/*.uf2 is gitignored; the RELEASED binary lives in UF2/ and is
# committed deliberately, not automatically.
add_custom_command(TARGET ${CARD_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          ${CMAKE_CURRENT_LIST_DIR}/FLASHME
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE_DIR:${CARD_NAME}>/${CARD_NAME}.uf2
          ${CMAKE_CURRENT_LIST_DIR}/FLASHME/${CARD_NAME}.uf2
  COMMENT "Copying ${CARD_NAME}.uf2 to FLASHME/")

# The Computer has no USB serial console in normal use, and enabling stdio over
# USB pulls in the whole TinyUSB stack.
pico_enable_stdio_usb(${CARD_NAME} 0)
