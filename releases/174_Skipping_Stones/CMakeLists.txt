cmake_minimum_required(VERSION 3.13)

# Keep the full redistribution notices beside generated firmware outputs.
configure_file("${CMAKE_CURRENT_LIST_DIR}/NOTICE.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/NOTICE.txt" COPYONLY)

if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}" CACHE PATH "Path to pico-sdk")
endif()

if(EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
else()
    include(pico_sdk_import.cmake)
endif()

project(SkippingStones C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(SkippingStones
    SkippingStones.cpp
)

target_include_directories(SkippingStones PRIVATE .)

target_compile_options(SkippingStones PRIVATE
    -O3
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_link_options(SkippingStones PRIVATE
    -Wl,--print-memory-usage
)

target_compile_definitions(SkippingStones PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_link_libraries(SkippingStones
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

pico_set_binary_type(SkippingStones copy_to_ram)
pico_enable_stdio_usb(SkippingStones 0)
pico_add_extra_outputs(SkippingStones)
