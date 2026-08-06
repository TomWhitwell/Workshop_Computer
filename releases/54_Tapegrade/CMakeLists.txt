cmake_minimum_required(VERSION 3.13)

# ============================================================
# Raspberry Pi Pico SDK
# ============================================================

set(PICO_SDK_PATH "/Users/adrianvos/pico-sdk")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# ============================================================
# Project
# ============================================================

project(Tapegrade C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ============================================================
# Initialise Pico SDK
# ============================================================

pico_sdk_init()

# ============================================================
# Executable
# ============================================================

add_executable(Tapegrade
    Tapegrade.cpp
)

# ============================================================
# Include paths
# ============================================================

target_include_directories(Tapegrade PRIVATE
    .
)

# ============================================================
# Libraries required by ComputerCard
# ============================================================

target_link_libraries(Tapegrade
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

# ============================================================
# Optimisation
# ============================================================

target_compile_options(Tapegrade PRIVATE
    -O3
    -ffast-math
)

# ============================================================
# Create UF2/BIN/etc
# ============================================================

pico_add_extra_outputs(Tapegrade)
