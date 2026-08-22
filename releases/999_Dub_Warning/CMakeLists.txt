cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(DubWarning C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(DubWarning
    main.cpp
)

target_include_directories(DubWarning PRIVATE
    .
)

target_compile_options(DubWarning PRIVATE
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_compile_definitions(DubWarning PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_link_options(DubWarning PRIVATE
    -Wl,--print-memory-usage
)

target_link_libraries(DubWarning
    pico_stdlib
    pico_unique_id
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    hardware_clocks
)

pico_enable_stdio_usb(DubWarning 0)
pico_enable_stdio_uart(DubWarning 0)
pico_set_binary_type(DubWarning copy_to_ram)
pico_add_extra_outputs(DubWarning)
