cmake_minimum_required(VERSION 3.13)

# ============================================================
# Raspberry Pi Pico SDK
# ============================================================

# Set this to your local pico-sdk path
set(PICO_SDK_PATH "/Users/adrianvos/pico-sdk")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# ============================================================
# Project
# ============================================================

project(WildPebble C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ============================================================
# Initialise Pico SDK
# ============================================================

pico_sdk_init()

# ============================================================
# Executable
# ============================================================

add_executable(WildPebble
    WildPebble.cpp
    usb_descriptors.c
)

# ============================================================
# Include paths
# ============================================================

target_include_directories(WildPebble PRIVATE
    .
)

# ============================================================
# Libraries
# ============================================================

target_compile_options(WildPebble PRIVATE
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_compile_definitions(WildPebble PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_link_options(WildPebble PRIVATE
    -Wl,--print-memory-usage
)

target_link_libraries(WildPebble
    pico_stdlib
    pico_unique_id
    pico_multicore

    tinyusb_device
    tinyusb_board
    
    hardware_adc
    hardware_pwm
    hardware_dma
    hardware_interp
    hardware_spi
    hardware_pio
    hardware_clocks
    hardware_flash
    hardware_i2c
)

# ============================================================
# Create UF2/BIN/etc
# ============================================================

pico_enable_stdio_usb(WildPebble 0)
pico_enable_stdio_uart(WildPebble 0)
pico_set_binary_type(WildPebble copy_to_ram)
pico_add_extra_outputs(WildPebble)
