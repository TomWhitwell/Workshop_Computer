# 8mu CV — a Music Thing Modular Workshop System Computer program card.
#
# This is a single-source card: EightMU.h carries the USB MIDI host driver and
# the host-mode TinyUSB configuration itself, so the only extra build needs are
# the TinyUSB host libraries and CFG_TUSB_CONFIG_FILE, which points TinyUSB at
# EightMU.h for its configuration.

cmake_minimum_required(VERSION 3.13)

# Explicit board, matching the other Workshop_Computer releases.
set(PICO_BOARD pico CACHE STRING "Board type")

if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake")
	include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
else()
	if (NOT DEFINED PICO_SDK_PATH)
		message(FATAL_ERROR "PICO_SDK_PATH not set and pico_sdk_import.cmake not found.")
	endif()
	include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

set(CARD_NAME "8mu_cv" CACHE STRING "ComputerCard firmware name (also the UF2 filename stem)")

project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(${CARD_NAME}
	${CMAKE_CURRENT_LIST_DIR}/main.cpp
)

target_compile_options(${CARD_NAME} PRIVATE
	-Wdouble-promotion -Wfloat-conversion -Wall -Wextra -O2)

target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# REQUIRED for the Workshop Computer's crystal.  Without it the card fails to
# boot on a cold power-up (it works from a warm reset, which is what makes the
# bug so confusing).  Do not remove.
target_compile_definitions(${CARD_NAME} PRIVATE
	PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
	CFG_TUSB_CONFIG_FILE="EightMU.h"
)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${CARD_NAME}
	pico_stdlib
	pico_multicore
	hardware_clocks
	hardware_dma
	hardware_i2c
	hardware_pwm
	hardware_adc
	hardware_spi
	tinyusb_host
	tinyusb_board
)

# Copy all code to RAM.  The card is tiny, so this is affordable, and it keeps
# flash-cache misses out of the 48kHz audio interrupt - core 1 is running the
# USB stack at the same time and would otherwise contend with it for the cache.
pico_set_binary_type(${CARD_NAME} copy_to_ram)

pico_add_extra_outputs(${CARD_NAME})

# The front jack is needed for USB *host* duty, so there is no USB serial
# console.  Enabling stdio over USB would fight the host stack for the
# controller and would interfere with the normalisation probe besides.
pico_enable_stdio_usb(${CARD_NAME} 0)
pico_enable_stdio_uart(${CARD_NAME} 0)

# Keep a copy of the built firmware beside the source, which is where the
# release expects to find it.
set(COMPUTERCARD_UF2_DIR "${CMAKE_CURRENT_LIST_DIR}/UF2" CACHE PATH "Directory to copy generated .uf2 files into")
add_custom_command(
	TARGET ${CARD_NAME}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${COMPUTERCARD_UF2_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy
		"${CMAKE_CURRENT_BINARY_DIR}/${CARD_NAME}.uf2"
		"${COMPUTERCARD_UF2_DIR}/${CARD_NAME}.uf2"
)
