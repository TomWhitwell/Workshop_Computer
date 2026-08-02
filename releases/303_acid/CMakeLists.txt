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
project(acid C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# Single-core card — the whole thing is one main.cpp. No USB / Grid / MIDI,
# so none of drumdrum's Core 1 source files or TinyUSB libraries are needed.
add_executable(acid
    main.cpp
)

# Warnings: catch accidental double-precision and other issues
target_compile_options(acid PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(acid PRIVATE -Wl,--print-memory-usage)

# ESSENTIAL: prevents crash-on-reset (oscillator startup timing)
target_compile_definitions(acid PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_include_directories(acid PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# hardware_i2c stays in: ComputerCard reads EEPROM calibration over I2C for
# CVOut1MIDINote()'s 1V/oct tracking. pico_multicore and the tinyusb_* libs
# that drumdrum needed are gone — this card never touches the second core or USB.
target_link_libraries(acid
    pico_unique_id pico_stdlib
    hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_clocks
)

# Copy all code to RAM — eliminates flash cache miss timing jitter
pico_set_binary_type(acid copy_to_ram)

pico_add_extra_outputs(acid)

# Copy the freshly-built UF2 up into the card folder so it tracks alongside
# the source and is downloadable from GitHub without a release build.
add_custom_command(TARGET acid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:acid>/acid.uf2"
        "${CMAKE_CURRENT_LIST_DIR}/acid.uf2"
    COMMENT "Copying acid.uf2 to card folder")

# USB stdio OFF — interferes with normalisation probe
pico_enable_stdio_usb(acid 0)
