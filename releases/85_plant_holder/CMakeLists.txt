cmake_minimum_required(VERSION 3.13)

# Use PICO_SDK_PATH from environment or set explicitly:
#   cmake -DPICO_SDK_PATH=~/projects/rp2040/pico-sdk-2.1 ..
include(pico_sdk_import.cmake)

project(plant_holder C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(plant_holder)

target_sources(plant_holder PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/plant_holder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/usb_midi_host.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_midi_host_app_driver.c
)

# Include current directory (tusb_config.h, placeholder_reverb.h, etc.)
# and ComputerCard library directory.
target_include_directories(plant_holder PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../Demonstrations+HelloWorlds/PicoSDK/ComputerCard
)

# Avoid startup timing problems
target_compile_definitions(plant_holder PUBLIC
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_compile_definitions(plant_holder PRIVATE
    PICO_DEFAULT_UART_BAUD_RATE=115200
)

pico_enable_stdio_uart(plant_holder 0)
pico_enable_stdio_usb(plant_holder 0)
pico_add_extra_outputs(plant_holder)

# Optimisation level comes from CMAKE_BUILD_TYPE, which the Pico SDK defaults
# to Release (-O3). Do not pin -O here: it overrides that and, at -O2, trips a
# false-positive -Warray-bounds inside the SDK's pwm_init().
target_compile_options(plant_holder PRIVATE
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)
target_link_options(plant_holder PRIVATE -Wl,--print-memory-usage)

target_link_libraries(plant_holder
    tinyusb_host
    tinyusb_board
    pico_unique_id
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
)
