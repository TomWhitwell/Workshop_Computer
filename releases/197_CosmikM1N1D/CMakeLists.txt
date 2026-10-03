cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(MINIMOOG_VOICE C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(MINIMOOG_VOICE
    MinimoogVoice.cpp
    MinimoogVoice_LUT.cpp
    MinimoogVoice_WaveLUT.cpp
    usb_descriptors.c
    usb_midi_host.c
    usb_midi_host_app_driver.c
)

target_include_directories(MINIMOOG_VOICE PRIVATE .)

target_link_libraries(MINIMOOG_VOICE
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
    hardware_flash
    hardware_i2c
)

target_compile_definitions(MINIMOOG_VOICE PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    MINIMOOG_OVERCLOCK_KHZ=192000
)

pico_enable_stdio_usb(MINIMOOG_VOICE 0)
pico_set_binary_type(MINIMOOG_VOICE copy_to_ram)

target_compile_options(MINIMOOG_VOICE PRIVATE
    -O3
    -ffast-math
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_link_options(MINIMOOG_VOICE PRIVATE -Wl,--print-memory-usage)
pico_add_extra_outputs(MINIMOOG_VOICE)
