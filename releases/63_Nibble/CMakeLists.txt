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

set(CARD_NAME "nibble")
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(${CARD_NAME}
  main.cpp
  levels.cpp
  keys.cpp
  drums.cpp
  looper.cpp
  fastmath.cpp)

target_compile_options(${CARD_NAME} PRIVATE
  -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)

# Worth watching every build: the card is nowhere near either limit, and a
# sudden jump means something large landed in the wrong section.
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# REQUIRED for the Workshop Computer's crystal. Without it the card fails to
# boot on a cold power-up (it works from a warm reset, which is exactly what
# makes the bug so confusing). Do not remove.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# Deliberately SHORTER than WorkshopBio's list. NIBBLE has:
#   no USB / web UI  -> no tinyusb_device, no tinyusb_board
#   no second core   -> no pico_multicore
#
# hardware_i2c/spi/pwm/adc/dma stay because ComputerCard.h itself needs them.
#
# hardware_flash also stays, and it is worth being precise about why, because
# the obvious reading is wrong: NIBBLE stores NOTHING in flash (the Four
# Voltages knob invalidates every learned level, so a saved calibration would
# silently restore a WRONG one -- see CLAUDE.md). The dependency is
# ComputerCard.h's own: it includes hardware/flash.h to read the CV output
# calibration the Workshop System writes at the factory. Dropping it fails the
# build, and there is no version of this card that does not want calibrated
# 1V/oct.
target_link_libraries(${CARD_NAME}
  pico_stdlib
  hardware_dma
  hardware_i2c
  hardware_pwm
  hardware_adc
  hardware_spi
  hardware_vreg
  hardware_flash)

pico_add_extra_outputs(${CARD_NAME})

# The Computer has no USB serial console in normal use, and enabling stdio over
# USB pulls in the whole TinyUSB stack we just went out of our way to avoid.
pico_enable_stdio_usb(${CARD_NAME} 0)
