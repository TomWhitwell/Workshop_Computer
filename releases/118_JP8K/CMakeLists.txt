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

set(PICO_BOARD pico CACHE STRING "Board type")
cmake_minimum_required(VERSION 3.13)

# Keep the full redistribution notices beside generated firmware outputs.
configure_file("${CMAKE_CURRENT_LIST_DIR}/NOTICE.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/NOTICE.txt" COPYONLY)
include(pico_sdk_import.cmake)

set(CARD_NAME jp8k_sandsquall)
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
pico_sdk_init()

add_executable(${CARD_NAME}
    main.cpp
    usb_descriptors.c
    usb_midi_host.c
    usb_midi_host_app_driver.c
)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${CARD_NAME}
    pico_unique_id
    pico_stdlib
    pico_multicore
    tinyusb_device
    tinyusb_host
    tinyusb_board
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    hardware_clocks
    hardware_flash
    hardware_sync
    hardware_vreg
)

pico_add_extra_outputs(${CARD_NAME})
pico_set_binary_type(${CARD_NAME} copy_to_ram)

target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(${CARD_NAME} 0)

target_compile_options(${CARD_NAME} PRIVATE
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
    -O3
)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)
