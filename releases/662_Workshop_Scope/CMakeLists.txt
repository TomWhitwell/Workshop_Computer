cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)
project(workshop_scope C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(scope main.c)

target_compile_definitions(scope PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

pico_enable_stdio_usb(scope 1)
pico_enable_stdio_uart(scope 0)

pico_add_extra_outputs(scope)
target_link_libraries(scope pico_stdlib pico_multicore hardware_adc hardware_dma hardware_spi hardware_pwm)
