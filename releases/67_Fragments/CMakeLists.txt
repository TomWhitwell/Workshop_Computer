cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(fragments C CXX ASM)

pico_sdk_init()

add_executable(fragments
  src/fragments.cpp
  src/usb_descriptors.c
)

target_include_directories(fragments PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/lib
)

target_compile_definitions(fragments PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
target_compile_options(fragments PRIVATE
  -Wall
  -Wextra
  -Wdouble-promotion
  -Wfloat-conversion
  -Os
)

pico_enable_stdio_uart(fragments 0)
pico_enable_stdio_usb(fragments 0)

target_link_libraries(fragments
  pico_stdlib
  hardware_spi
  hardware_dma
  hardware_adc
  hardware_pwm
  hardware_i2c
  hardware_timer
  hardware_clocks
  hardware_flash
  pico_unique_id
  pico_multicore
  tinyusb_device
  tinyusb_host
  tinyusb_board
)

pico_set_binary_type(fragments copy_to_ram)
pico_add_extra_outputs(fragments)
