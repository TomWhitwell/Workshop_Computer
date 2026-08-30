cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(CS80 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(CS80
    CS80.cpp
    CS80_LUT.cpp
    usb_descriptors.c
    usb_midi_host.c
    usb_midi_host_app_driver.c
)

target_include_directories(CS80 PRIVATE
    .
)

target_link_libraries(CS80
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

target_compile_definitions(CS80 PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    CS80_OVERCLOCK_KHZ=192000
)

pico_enable_stdio_usb(CS80 0)
pico_set_binary_type(CS80 copy_to_ram)

target_compile_options(CS80 PRIVATE
    -O3
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_link_options(CS80 PRIVATE
    -Wl,--print-memory-usage
)

pico_add_extra_outputs(CS80)
