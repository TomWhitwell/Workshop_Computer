cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico)
include(pico_sdk_import.cmake)

project(chord_organ C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(chord_organ
    main.cpp
)

target_link_libraries(chord_organ
    pico_stdlib
    hardware_gpio
    hardware_pwm
    hardware_adc
    hardware_dma
    hardware_spi
    hardware_i2c
    hardware_irq
    hardware_clocks
)

pico_set_binary_type(chord_organ copy_to_ram)
pico_add_extra_outputs(chord_organ)

target_compile_definitions(chord_organ PUBLIC PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(chord_organ 1)
pico_enable_stdio_uart(chord_organ 0)

target_compile_options(chord_organ PRIVATE
    -Wall
    -Wextra
    -O2
    -ffast-math
    -funroll-loops
)

target_include_directories(chord_organ PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
