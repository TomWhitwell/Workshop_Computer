cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk" CACHE PATH "Local Pico SDK path")

include(pico_sdk_import.cmake)

project(stretchcore C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(stretchcore
    src/main.cpp
    src/BreakyAudioBank.cpp
    src/BreakySampleManager.cpp
)

target_include_directories(stretchcore PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_options(stretchcore PRIVATE
    -Wall
    -Wextra
    -Wdouble-promotion
    -Wfloat-conversion
)

target_link_options(stretchcore PRIVATE
    -Wl,--print-memory-usage
)

target_compile_definitions(stretchcore PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    BREAKY_FIRMWARE_RESERVE=163840u
    USBD_PID=0x4011
    USBD_MANUFACTURER="infinitedigits"
    USBD_PRODUCT="stretchcore"
)

target_link_libraries(stretchcore
    pico_unique_id
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    hardware_clocks
    hardware_flash
)

pico_set_binary_type(stretchcore copy_to_ram)
pico_enable_stdio_usb(stretchcore 1)
pico_enable_stdio_uart(stretchcore 0)
pico_add_extra_outputs(stretchcore)

add_custom_command(
    TARGET stretchcore
    POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DBIN_FILE="$<TARGET_FILE_DIR:stretchcore>/stretchcore.bin"
        -DFIRMWARE_RESERVE=163840
        -P "${CMAKE_CURRENT_LIST_DIR}/cmake/check_firmware_reserve.cmake"
)
