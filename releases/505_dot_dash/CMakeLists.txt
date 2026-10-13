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
project(dot_dash C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(dot_dash
    main.cpp
)

# Catch accidental double-precision floats and other warnings early.
target_compile_options(dot_dash PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(dot_dash PRIVATE -Wl,--print-memory-usage)

# ESSENTIAL: prevents crash-on-reset (oscillator startup timing).
target_compile_definitions(dot_dash PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_include_directories(dot_dash PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(dot_dash
    pico_unique_id pico_stdlib pico_multicore
    hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_clocks
    tinyusb_host tinyusb_board
)

# Copy all code to RAM - eliminates flash cache miss timing jitter. This card
# is tiny, so there is plenty of room.
pico_set_binary_type(dot_dash copy_to_ram)

pico_add_extra_outputs(dot_dash)

# Copy the freshly-built UF2 up beside the source so it stays committed.
add_custom_command(TARGET dot_dash POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:dot_dash>/dot_dash.uf2"
        "${CMAKE_CURRENT_LIST_DIR}/dot_dash.uf2"
    COMMENT "Copying dot_dash.uf2 to release folder")

# USB stdio OFF - it interferes with the normalisation probe.
pico_enable_stdio_usb(dot_dash 0)
pico_enable_stdio_uart(dot_dash 0)
