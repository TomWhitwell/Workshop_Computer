cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Pull in Raspberry Pi Pico SDK (uses PICO_SDK_PATH env var or finds it automatically)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required.")
endif()

project(CA_Sequencer C CXX ASM)
pico_sdk_init()

add_executable(CA_Sequencer main.cpp)

pico_set_program_name(CA_Sequencer "CA_Sequencer")
pico_set_program_version(CA_Sequencer "1.0")

# Add warnings
 target_compile_options(CA_Sequencer PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
 target_link_options(CA_Sequencer PRIVATE -Wl,--print-memory-usage)

# Give oscillator more time to start
target_compile_definitions(CA_Sequencer PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

# Include ComputerCard library
target_include_directories(CA_Sequencer PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../Demonstrations+HelloWorlds/PicoSDK/ComputerCard
)

target_link_libraries(CA_Sequencer
    pico_unique_id
    pico_stdlib
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
)

pico_enable_stdio_uart(CA_Sequencer 0)
pico_enable_stdio_usb(CA_Sequencer 0)
pico_add_extra_outputs(CA_Sequencer)
