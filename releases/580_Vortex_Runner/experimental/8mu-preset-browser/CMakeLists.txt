cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(VortexRunner C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(VortexRunner
    CS80.cpp
    CS80_LUT.cpp
    usb_descriptors.c
    usb_midi_host.c
    usb_midi_host_app_driver.c
)

target_include_directories(VortexRunner PRIVATE
    .
)

target_link_libraries(VortexRunner
    pico_stdlib
    pico_unique_id
    pico_multicore
    tinyusb_device
    tinyusb_host
    tinyusb_board
    hardware_adc
    hardware_pwm
    hardware_dma
    hardware_spi
    hardware_clocks
    hardware_i2c
)

target_compile_definitions(VortexRunner PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    CS80_OVERCLOCK_KHZ=192000
)

pico_enable_stdio_usb(VortexRunner 0)
pico_set_binary_type(VortexRunner copy_to_ram)

target_compile_options(VortexRunner PRIVATE
    -O3
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_link_options(VortexRunner PRIVATE
    -Wl,--print-memory-usage
)

pico_add_extra_outputs(VortexRunner)
