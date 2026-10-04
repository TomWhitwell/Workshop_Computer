cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

set(CARD_NAME "cvseq")
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(${CARD_NAME} main.cpp usb_descriptors.c)

target_compile_options(${CARD_NAME} PRIVATE
  -Wdouble-promotion -Wfloat-conversion -Wall -Wextra -O2)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# Needed for the Workshop Computer's crystal to start reliably from cold
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

# tusb_config.h (next to main.cpp) enables USB host and device modes; the
# host side and the usb_midi_host driver come from EightMU.h

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${CARD_NAME}
  pico_stdlib
  pico_unique_id
  pico_multicore
  hardware_dma
  hardware_i2c
  hardware_pwm
  hardware_adc
  hardware_spi
  tinyusb_host
  tinyusb_device
  tinyusb_board)

# Run from RAM, so core1's USB activity can't upset the audio interrupt
# through flash cache misses
pico_set_binary_type(${CARD_NAME} copy_to_ram)

pico_add_extra_outputs(${CARD_NAME})
pico_enable_stdio_usb(${CARD_NAME} 0)
pico_enable_stdio_uart(${CARD_NAME} 0)
