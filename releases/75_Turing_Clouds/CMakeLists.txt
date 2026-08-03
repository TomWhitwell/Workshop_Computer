cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required.")
endif()

project(Turing_Clouds C CXX ASM)
pico_sdk_init()

add_executable(Turing_Clouds main.cpp)

pico_set_program_name(Turing_Clouds "Turing_Clouds")
pico_set_program_version(Turing_Clouds "1.0")

target_compile_options(Turing_Clouds PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(Turing_Clouds PRIVATE -Wl,--print-memory-usage)

target_compile_definitions(Turing_Clouds PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_include_directories(Turing_Clouds PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../Demonstrations+HelloWorlds/PicoSDK/ComputerCard
)

target_link_libraries(Turing_Clouds
    pico_unique_id
    pico_stdlib
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
)

pico_enable_stdio_uart(Turing_Clouds 0)
pico_enable_stdio_usb(Turing_Clouds 0)
pico_add_extra_outputs(Turing_Clouds)
