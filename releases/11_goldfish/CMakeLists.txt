# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
project(goldfish C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()


add_executable(goldfish)
target_include_directories(goldfish PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(goldfish pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_flash)
pico_add_extra_outputs(goldfish)
target_sources(goldfish PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/goldfish_stream.c)
target_compile_definitions(goldfish PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(goldfish 0)
