cmake_minimum_required(VERSION 3.13)

# ============================================================
# Raspberry Pi Pico SDK
# ============================================================

set(PICO_SDK_PATH "/Users/adrianvos/pico-sdk")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# ============================================================
# Project
# ============================================================

project(C1ZZL3_FULL_DUAL_OSCILLATORS C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ============================================================
# Initialise Pico SDK
# ============================================================

pico_sdk_init()

# ============================================================
# Executable
# ============================================================

add_executable(C1ZZL3_FULL_DUAL_OSCILLATORS
    C1ZZL3.cpp
    C1ZZL3_LUT.cpp
    usb_descriptors.c
    usb_midi_host.c
    usb_midi_host_app_driver.c
)

# ============================================================
# Include paths
# ============================================================

target_include_directories(C1ZZL3_FULL_DUAL_OSCILLATORS PRIVATE
    .
)

# ============================================================
# Libraries required by ComputerCard
# ============================================================

target_link_libraries(C1ZZL3_FULL_DUAL_OSCILLATORS
    pico_stdlib
    pico_unique_id
    pico_multicore
    tinyusb_device
    tinyusb_host
    tinyusb_board

    hardware_adc
    hardware_pwm
    hardware_dma
    hardware_spi
    hardware_clocks
    hardware_flash
    hardware_i2c
)

# ============================================================
# Workshop Computer hardware settings
# ============================================================

target_compile_definitions(C1ZZL3_FULL_DUAL_OSCILLATORS PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    C1ZZL3_OVERCLOCK_KHZ=192000
)

pico_enable_stdio_usb(C1ZZL3_FULL_DUAL_OSCILLATORS 0)
pico_set_binary_type(C1ZZL3_FULL_DUAL_OSCILLATORS copy_to_ram)

# ============================================================
# Optimisation
# ============================================================

target_compile_options(C1ZZL3_FULL_DUAL_OSCILLATORS PRIVATE
    -O3
    -ffast-math
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_link_options(C1ZZL3_FULL_DUAL_OSCILLATORS PRIVATE
    -Wl,--print-memory-usage
)

# ============================================================
# Create UF2/BIN/etc
# ============================================================

pico_add_extra_outputs(C1ZZL3_FULL_DUAL_OSCILLATORS)
