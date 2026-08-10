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

project(CastleProcess C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ============================================================
# Initialise Pico SDK
# ============================================================

pico_sdk_init()

# ============================================================
# Executable
# ============================================================

add_executable(CastleProcess
    CastleProcess.cpp
)

# ============================================================
# Include paths
# ============================================================

target_include_directories(CastleProcess PRIVATE
    .
)

target_compile_definitions(CastleProcess PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

# ============================================================
# Libraries
# ============================================================

target_link_libraries(CastleProcess
    pico_stdlib
    pico_unique_id
    
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

pico_enable_stdio_usb(CastleProcess 0)
pico_set_binary_type(CastleProcess copy_to_ram)

# ============================================================
# Create UF2/BIN/etc
# ============================================================

pico_add_extra_outputs(CastleProcess)
