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
# reuse the parent applet's pico_sdk_import.cmake
include(${CMAKE_CURRENT_LIST_DIR}/../pico_sdk_import.cmake)

set(CARD_NAME gba_loopback)
project(gba_loopback C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# The diagnostic reuses the real transport (gba_spi_init/gba_spi_xfer32) from the parent
# gba_multiboot.cpp, and the parent's gba_spi.pio, so a pass here validates the real path.
add_executable(${CARD_NAME}
    loopback.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../gba_multiboot.cpp)
pico_generate_pio_header(${CARD_NAME} ${CMAKE_CURRENT_LIST_DIR}/../gba_spi.pio)
target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(${CARD_NAME}
    pico_unique_id
    pico_stdlib
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    hardware_pio
    pico_multicore)
pico_add_extra_outputs(${CARD_NAME})
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(${CARD_NAME} 0)
target_compile_options(${CARD_NAME} PRIVATE -Wall -Wextra)

# Second diagnostic: live GBA handshake probe (shows the GBA's actual reply on the LEDs).
add_executable(gba_probe
    gba_probe.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../gba_multiboot.cpp)
pico_generate_pio_header(gba_probe ${CMAKE_CURRENT_LIST_DIR}/../gba_spi.pio)
target_include_directories(gba_probe PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(gba_probe
    pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm
    hardware_adc hardware_spi hardware_pio pico_multicore)
pico_add_extra_outputs(gba_probe)
target_compile_definitions(gba_probe PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(gba_probe 0)
target_compile_options(gba_probe PRIVATE -Wall -Wextra)

# Staged, scope-friendly bring-up diagnostic (switch selects stage 1/2/3).
add_executable(bringup
    bringup.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../gba_multiboot.cpp)
pico_generate_pio_header(bringup ${CMAKE_CURRENT_LIST_DIR}/../gba_spi.pio)
target_include_directories(bringup PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(bringup
    pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm
    hardware_adc hardware_spi hardware_pio pico_multicore)
pico_add_extra_outputs(bringup)
target_compile_definitions(bringup PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(bringup 0)
target_compile_options(bringup PRIVATE -Wall -Wextra)

# LED-only link diagnostic: click-to-advance tests, no scope and no held switch.
add_executable(linkcheck
    linkcheck.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../gba_multiboot.cpp)
pico_generate_pio_header(linkcheck ${CMAKE_CURRENT_LIST_DIR}/../gba_spi.pio)
target_include_directories(linkcheck PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(linkcheck
    pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm
    hardware_adc hardware_spi hardware_pio pico_multicore)
pico_add_extra_outputs(linkcheck)
target_compile_definitions(linkcheck PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(linkcheck 0)
target_compile_options(linkcheck PRIVATE -Wall -Wextra)

# Cable/crossover discriminator — uses Pulse In 2 as a second listening port.
add_executable(cablecheck
    cablecheck.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../gba_multiboot.cpp)
pico_generate_pio_header(cablecheck ${CMAKE_CURRENT_LIST_DIR}/../gba_spi.pio)
target_include_directories(cablecheck PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(cablecheck
    pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm
    hardware_adc hardware_spi hardware_pio pico_multicore)
pico_add_extra_outputs(cablecheck)
target_compile_definitions(cablecheck PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(cablecheck 0)
target_compile_options(cablecheck PRIVATE -Wall -Wextra)

# GBATEK handshake walker — reports which multiboot stage is reached.
add_executable(handshake
    handshake.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../gba_multiboot.cpp)
pico_generate_pio_header(handshake ${CMAKE_CURRENT_LIST_DIR}/../gba_spi.pio)
target_include_directories(handshake PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(handshake
    pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm
    hardware_adc hardware_spi hardware_pio pico_multicore)
pico_add_extra_outputs(handshake)
target_compile_definitions(handshake PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(handshake 0)
target_compile_options(handshake PRIVATE -Wall -Wextra)

# Link bandwidth / latency measurement. Runs the link on core 1 like the applet does.
add_executable(bandwidth
    bandwidth.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../gba_multiboot.cpp)
pico_generate_pio_header(bandwidth ${CMAKE_CURRENT_LIST_DIR}/../gba_spi.pio)
target_include_directories(bandwidth PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(bandwidth
    pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm
    hardware_adc hardware_spi hardware_pio pico_multicore)
pico_add_extra_outputs(bandwidth)
target_compile_definitions(bandwidth PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(bandwidth 0)
target_compile_options(bandwidth PRIVATE -Wall -Wextra)

# Manual multiboot speed test: one speed at a time, chosen by hand.
add_executable(mbrate
    mbrate.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../gba_multiboot.cpp)
pico_generate_pio_header(mbrate ${CMAKE_CURRENT_LIST_DIR}/../gba_spi.pio)
target_include_directories(mbrate PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(mbrate
    pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm
    hardware_adc hardware_spi hardware_pio pico_multicore)
pico_add_extra_outputs(mbrate)
target_compile_definitions(mbrate PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(mbrate 0)
target_compile_options(mbrate PRIVATE -Wall -Wextra)

# Post-boot link speed test: boots at 100 kHz, then steps the live rate by hand.
add_executable(linkrate
    linkrate.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../gba_multiboot.cpp)
pico_generate_pio_header(linkrate ${CMAKE_CURRENT_LIST_DIR}/../gba_spi.pio)
target_include_directories(linkrate PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(linkrate
    pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm
    hardware_adc hardware_spi hardware_pio pico_multicore)
pico_add_extra_outputs(linkrate)
target_compile_definitions(linkrate PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(linkrate 0)
target_compile_options(linkrate PRIVATE -Wall -Wextra)
