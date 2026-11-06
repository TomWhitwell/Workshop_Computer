cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(shift_voice C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(shift_voice
    main.cpp
    usb_descriptors.c
)

# Run everything from RAM: avoids flash-cache timing jitter in the audio
# interrupt, and lets audio keep running while settings are saved to flash.
pico_set_binary_type(shift_voice copy_to_ram)

target_include_directories(shift_voice PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_compile_options(shift_voice PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra -O2)
target_link_options(shift_voice PRIVATE -Wl,--print-memory-usage)

# Give the crystal oscillator more time to start; some boards fail after reset without it
target_compile_definitions(shift_voice PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_link_libraries(shift_voice
    pico_unique_id pico_stdlib pico_multicore
    hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_flash
    tinyusb_device tinyusb_board
)

pico_enable_stdio_usb(shift_voice 0)
pico_enable_stdio_uart(shift_voice 0)
pico_add_extra_outputs(shift_voice)
