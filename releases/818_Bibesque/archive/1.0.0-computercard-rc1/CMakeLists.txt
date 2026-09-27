cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(bib_workshop C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
pico_sdk_init()

add_executable(bib_workshop bib.cpp)
target_include_directories(bib_workshop PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(bib_workshop
    pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm
    hardware_adc hardware_spi)

# Keeping all audio code in RAM avoids unpredictable flash-cache stalls in
# ComputerCard's 48 kHz interrupt.
pico_set_binary_type(bib_workshop copy_to_ram)
pico_add_extra_outputs(bib_workshop)
target_compile_definitions(bib_workshop PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(bib_workshop 0)
target_compile_options(bib_workshop PRIVATE -O3 -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(bib_workshop PRIVATE -Wl,--print-memory-usage)
