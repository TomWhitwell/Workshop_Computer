# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

set(CARD_NAME uncertainty)
project(uncertainty C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
pico_sdk_init()

# Uncertainty: a Buchla 266 Source of Uncertainty tribute (noise + FRV +
# QRV), plus a Buchla-lineage wavefolder and a pulse alternator. See dsp/
# for the individual blocks and main.cpp for how they're wired to the panel.
add_executable(${CARD_NAME}
    main.cpp
)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${CARD_NAME}
    pico_unique_id
    pico_stdlib
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    pico_multicore
)

pico_add_extra_outputs(${CARD_NAME})

# Run entirely from RAM: XIP flash cache misses mid-ProcessSample can stall
# for microseconds, which is fatal inside a ~20us/sample budget.
pico_set_binary_type(${CARD_NAME} copy_to_ram)

# Give the oscillator more time to start - some boards won't run without this.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(${CARD_NAME} 0)

target_compile_options(${CARD_NAME} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra -O3)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)
