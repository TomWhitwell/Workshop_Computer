cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(knots C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(knots)

target_compile_options(knots PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(knots PRIVATE -Wl,--print-memory-usage)

# Give the RP2040 crystal time to start cleanly after reset so the card
# reliably boots on power cycles as well as first flash.
target_compile_definitions(knots PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_include_directories(knots PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(knots PUBLIC ${CMAKE_CURRENT_LIST_DIR}/knots/src)
target_include_directories(knots PUBLIC ${CMAKE_CURRENT_LIST_DIR}/knots/src/usb)

target_sources(knots PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/core/control_router.cpp
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/core/midi_worker.cpp
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/engines/bender_engine.cpp
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/engines/cumulus_engine.cpp
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/engines/din_sum_engine.cpp
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/engines/engine_registry.cpp
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/engines/floatable_engine.cpp
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/engines/losenge_engine.cpp
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/engines/sawsome_engine.cpp
  ${CMAKE_CURRENT_LIST_DIR}/knots/src/usb/usb_descriptors.c
)

target_link_libraries(knots
  pico_unique_id
  pico_stdlib
  pico_multicore
  tinyusb_device
  tinyusb_board
  hardware_dma
  hardware_i2c
  hardware_pwm
  hardware_adc
  hardware_spi
  hardware_interp
  hardware_flash
  hardware_sync
)

pico_enable_stdio_usb(knots 0)
pico_enable_stdio_uart(knots 0)
pico_add_extra_outputs(knots)
