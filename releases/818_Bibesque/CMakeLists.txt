cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(bib_workshop C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
pico_sdk_init()

add_executable(bib_workshop main.cpp tanh_table.c)
target_link_libraries(bib_workshop
    pico_multicore pico_stdlib hardware_adc hardware_dma hardware_pwm hardware_spi)

# The complete runtime is copied to RAM so flash-cache stalls cannot disturb
# the sample service routine.
pico_set_binary_type(bib_workshop copy_to_ram)
pico_add_extra_outputs(bib_workshop)
target_compile_definitions(bib_workshop PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(bib_workshop 0)
target_compile_options(bib_workshop PRIVATE -O3 -Wall -Wextra -Wdouble-promotion -Wfloat-conversion)
target_link_options(bib_workshop PRIVATE -Wl,--print-memory-usage)
