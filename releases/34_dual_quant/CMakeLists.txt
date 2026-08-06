cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(dual_quant_pitch)

pico_sdk_init()

add_executable(dual_quant_pitch
    dual_quant_pitch.cpp
)

target_include_directories(dual_quant_pitch PRIVATE
    ../../Demonstrations+HelloWorlds/PicoSDK/ComputerCard
)

target_link_libraries(dual_quant_pitch
    pico_stdlib
    pico_unique_id
    hardware_adc
    hardware_spi
    hardware_pwm
    hardware_dma
    hardware_i2c
)

target_compile_definitions(dual_quant_pitch PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_compile_options(dual_quant_pitch PRIVATE
    -Wall
    -Wextra
    -Wdouble-promotion
    -Wfloat-conversion
)

target_link_options(dual_quant_pitch PRIVATE
    -Wl,--print-memory-usage
)

# Strongly recommended for stable ISR timing.
# Running entirely from RAM avoids flash cache stalls.
pico_set_binary_type(dual_quant_pitch copy_to_ram)

pico_enable_stdio_usb(dual_quant_pitch 0)
pico_enable_stdio_uart(dual_quant_pitch 0)

pico_add_extra_outputs(dual_quant_pitch)
