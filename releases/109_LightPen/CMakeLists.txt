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

set(CARD_NAME "lightpen")
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# Every .cpp must be listed here: a missing one only shows up as a link error.
add_executable(${CARD_NAME}
  main.cpp
  fastmath.cpp
  sensors.cpp
  tape.cpp
  modes/mirror.cpp
  modes/triad.cpp
  modes/barcode.cpp
  modes/tapescrub.cpp
  modes/synesthesia.cpp
  modes/hueorgan.cpp
  modes/jog.cpp
  modes/prism.cpp)

target_compile_options(${CARD_NAME} PRIVATE
  -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)

# Worth watching every build: a sudden jump means something large landed in
# the wrong section.
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# REQUIRED for the Workshop Computer's crystal. Without it the card fails to
# boot on a cold power-up (it works from a warm reset, which is exactly what
# makes the bug so confusing). Do not remove.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

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

# The Computer has no USB serial console in normal use, and enabling stdio
# over USB pulls in the whole TinyUSB stack.
pico_enable_stdio_usb(${CARD_NAME} 0)
