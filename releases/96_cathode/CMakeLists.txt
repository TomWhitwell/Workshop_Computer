# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

set(CARD_NAME cathode_ray)
project(cathode_ray C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# Shared build settings applied to each target (PAL default + NTSC variant). Both are built
# from the SAME main.cpp / composite.pio; the NTSC one just adds -DTV_NTSC (see the timing
# #ifdef block in main.cpp). Produces cathode_ray.uf2 and cathode_ray_ntsc.uf2.
function(cathode_target tgt)
    pico_generate_pio_header(${tgt} ${CMAKE_CURRENT_LIST_DIR}/composite.pio)
    target_include_directories(${tgt} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_link_libraries(${tgt}
        pico_unique_id
        pico_stdlib
        hardware_dma
        hardware_i2c
        hardware_pwm
        hardware_adc
        hardware_spi
        hardware_pio
        pico_multicore)
    pico_add_extra_outputs(${tgt})
    target_compile_definitions(${tgt} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
    pico_enable_stdio_usb(${tgt} 0)
    target_compile_options(${tgt} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
    target_link_options(${tgt} PRIVATE -Wl,--print-memory-usage)
endfunction()

# PAL (default, primary target) → cathode_ray.uf2
add_executable(${CARD_NAME} main.cpp)
cathode_target(${CARD_NAME})

# NTSC variant → cathode_ray_ntsc.uf2 (same source, -DTV_NTSC)
add_executable(cathode_ray_ntsc main.cpp)
cathode_target(cathode_ray_ntsc)
target_compile_definitions(cathode_ray_ntsc PRIVATE TV_NTSC)
