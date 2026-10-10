cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(workshop_blueberry C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
pico_sdk_init()

add_executable(workshop_blueberry main.cpp)
target_include_directories(workshop_blueberry PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(workshop_blueberry
    pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi)

pico_set_binary_type(workshop_blueberry copy_to_ram)
pico_add_extra_outputs(workshop_blueberry)
target_compile_definitions(workshop_blueberry PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(workshop_blueberry 0)
target_compile_options(workshop_blueberry PRIVATE -O3 -Wall -Wextra -Wdouble-promotion -Wfloat-conversion)
target_link_options(workshop_blueberry PRIVATE -Wl,--print-memory-usage)
