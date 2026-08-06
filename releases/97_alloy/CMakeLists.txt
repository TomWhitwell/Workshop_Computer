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

set(CARD_NAME alloy)
project(alloy C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
pico_sdk_init()

# Alloy: a cross-modulator in the spirit of Mutable Instruments Warps
# (and its parasites firmware), rewritten in fixed point for the FPU-less
# RP2040. The audio path is the integer implementation in dsp/.
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

# Match the other cards' warning set. Keep the double-promotion /
# float-conversion warnings on to catch accidental doubles (expensive
# without an FPU on RP2040).
target_compile_options(${CARD_NAME} PRIVATE -Wall -Wextra -O3)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)
