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
project(drumdrum C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(drumdrum
    main.cpp
    usb_core1.cpp
    usb_descriptors.c
    midi_sysex.cpp
    monome_mext.c
    grid_ui.cpp
    midi_host.cpp
)

# Warnings: catch accidental double-precision and other issues
target_compile_options(drumdrum PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(drumdrum PRIVATE -Wl,--print-memory-usage)

# ESSENTIAL: prevents crash-on-reset (oscillator startup timing)
target_compile_definitions(drumdrum PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_include_directories(drumdrum PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(drumdrum
    pico_unique_id pico_stdlib pico_multicore
    hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_clocks
    tinyusb_device tinyusb_host tinyusb_board
)

# Copy all code to RAM — eliminates flash cache miss timing jitter
pico_set_binary_type(drumdrum copy_to_ram)

pico_add_extra_outputs(drumdrum)

# Copy the freshly-built UF2 up to the repo root so it tracks alongside
# the source. The repo's .gitignore explicitly allows /drumdrum.uf2 so
# this stays committed and downloadable from GitHub without a release.
add_custom_command(TARGET drumdrum POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:drumdrum>/drumdrum.uf2"
        "${CMAKE_CURRENT_LIST_DIR}/drumdrum.uf2"
    COMMENT "Copying drumdrum.uf2 to repo root")

# USB stdio OFF — interferes with normalisation probe
pico_enable_stdio_usb(drumdrum 0)
