cmake_minimum_required(VERSION 3.13)

if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}" CACHE PATH "Path to pico-sdk")
endif()

if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR "Set PICO_SDK_PATH to a pico-sdk checkout.")
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(TapeBias C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(TapeBias
    TapeBias.cpp
)

target_include_directories(TapeBias PRIVATE .)

target_link_libraries(TapeBias
    pico_stdlib
    pico_unique_id
    hardware_adc
    hardware_pwm
    hardware_dma
    hardware_spi
    hardware_clocks
    hardware_flash
    hardware_i2c
)

# Stable restart on Workshop Computer hardware.
target_compile_definitions(TapeBias PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_compile_options(TapeBias PRIVATE
    -O3
    -ffast-math
)

pico_set_binary_type(TapeBias copy_to_ram)
pico_add_extra_outputs(TapeBias)
