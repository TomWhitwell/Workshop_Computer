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
cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)

set(CARD_NAME "tract8")
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(${CARD_NAME}
  main.cpp
  voder.cpp
  midi8mu.cpp
  usb_core1.cpp
  usb_midi_host.c
  usb_midi_host_app_driver.c)

target_compile_options(${CARD_NAME} PRIVATE
  -Wdouble-promotion -Wfloat-conversion -Wall -Wextra -O2)

# Worth watching every build: a sudden jump means something large landed in
# the wrong section. The filter bank state is tiny (8 bands x 4 words), so
# nearly all of the RAM figure is TinyUSB's host buffers.
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# REQUIRED for the Workshop Computer's crystal. Without it the card fails to
# boot on a cold power-up (it works from a warm reset, which is exactly what
# makes the bug so confusing). Do not remove.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

# Every biquad tap is a Q15 coefficient times a Q15 state word, and the
# accumulated 3-tap sum genuinely needs 64 bits: one product reaches 1.3e9
# (fits int32) but three of them sum to 4.0e9 (does not). Checked in
# tools/filter_check.py. The Cortex-M0+ has no 64-bit multiply, so each one
# is a call to __aeabi_lmul - which lives in FLASH by default. At 8 bands
# x 4 products x 48 kHz that is 1.5 million calls a second.
#
# tools/budget_check.py puts the difference at ~40 points of DSP load,
# which is the difference between this card fitting in its budget and not.
#
# Strictly this is belt-and-braces alongside copy_to_ram below, which
# already relocates everything: verified with
#   arm-none-eabi-nm build/tract8.elf | grep aeabi_lmul
# giving 0x20009324, i.e. RAM. It stays because it is the flag that
# expresses the intent, and because it keeps the guarantee if anyone ever
# takes copy_to_ram off to reclaim RAM.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_INT64_OPS_IN_RAM=1)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${CARD_NAME}
  pico_stdlib
  pico_multicore
  hardware_dma
  hardware_i2c
  hardware_pwm
  hardware_adc
  hardware_spi
  hardware_vreg
  hardware_flash
  hardware_clocks
  tinyusb_host
  tinyusb_board)

# Copy all code to RAM - eliminates flash cache miss timing jitter, which
# matters here because Core 1's USB transfers and Core 0's audio ISR would
# otherwise contend for the XIP cache. 26_clockwork does the same.
#
# This is affordable because the card is small: 45 KB of text against
# 256 KB of RAM, leaving RAM at 18.7% with the USB host buffers included.
pico_set_binary_type(${CARD_NAME} copy_to_ram)

pico_add_extra_outputs(${CARD_NAME})

# The Computer has no USB serial console in normal use, and the front jack is
# needed for USB *host* duty here. Enabling stdio over USB would fight the
# host stack for the controller, and it interferes with the normalisation
# probe besides.
pico_enable_stdio_usb(${CARD_NAME} 0)
pico_enable_stdio_uart(${CARD_NAME} 0)
