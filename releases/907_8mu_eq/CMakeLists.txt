cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
project(8mu_eq C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(8mu_eq main.cpp)
target_compile_options(8mu_eq PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(8mu_eq PRIVATE -Wl,--print-memory-usage)
target_compile_definitions(8mu_eq PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
target_include_directories(8mu_eq PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(8mu_eq pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi)

# EightMU.h carries the usb_midi_host driver and the host-mode TinyUSB config
target_compile_definitions(8mu_eq PRIVATE CFG_TUSB_CONFIG_FILE="EightMU.h")
target_link_libraries(8mu_eq pico_multicore tinyusb_host tinyusb_board)

pico_set_binary_type(8mu_eq copy_to_ram)
pico_enable_stdio_usb(8mu_eq 0)
pico_add_extra_outputs(8mu_eq)
