# Bootleg Radio (538) — a Music Thing Modular Workshop System Computer program card.
#
# A lo-fi bootleg tribute to Radio Music: plays WAV files from a USB stick, with
# a control voltage picking the station and another setting the start point.
#
# This is a USB *host* card, so it needs the TinyUSB host libraries and a recent
# TinyUSB. Older TinyUSB (the 0.18/0.20 bundled with many Pico SDKs) reads USB
# sticks at only ~64kB/s, far too slow for audio, so the build stops with a
# clear message rather than producing silent firmware. Configure with
#   -DPICO_TINYUSB_PATH=/path/to/tinyusb
# to point at a newer TinyUSB than the SDK's own.

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

set(CARD_NAME "bootleg_radio" CACHE STRING "ComputerCard firmware name (also the UF2 filename stem)")

project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# --- TinyUSB version check ---------------------------------------------------
# This card needs TinyUSB 0.21 or later, which has double-buffered RP2040 host
# transfers (~1MB/s). Older TinyUSB manages only ~64kB/s of USB mass storage
# reads, which cannot keep up with 48kHz audio. The version is read from the
# TinyUSB source itself, so no extra options are needed once the SDK bundles a
# new enough copy.
if (PICO_TINYUSB_PATH)
	set(_tinyusb_dir ${PICO_TINYUSB_PATH})
else()
	set(_tinyusb_dir ${PICO_SDK_PATH}/lib/tinyusb)
endif()
set(_tinyusb_major 0)
set(_tinyusb_minor 0)
if (EXISTS ${_tinyusb_dir}/src/tusb_option.h)
	file(STRINGS ${_tinyusb_dir}/src/tusb_option.h _tinyusb_ver REGEX "^#define TUSB_VERSION_(MAJOR|MINOR) ")
	foreach(_line ${_tinyusb_ver})
		if (_line MATCHES "TUSB_VERSION_MAJOR +([0-9]+)")
			set(_tinyusb_major ${CMAKE_MATCH_1})
		elseif (_line MATCHES "TUSB_VERSION_MINOR +([0-9]+)")
			set(_tinyusb_minor ${CMAKE_MATCH_1})
		endif()
	endforeach()
endif()

if (_tinyusb_major LESS 1 AND _tinyusb_minor LESS 21)
	if (EXISTS ${_tinyusb_dir}/src/tusb_option.h)
		set(_tinyusb_found "found ${_tinyusb_major}.${_tinyusb_minor}")
	else()
		set(_tinyusb_found "found no TinyUSB (src/tusb_option.h is missing)")
	endif()
	message(FATAL_ERROR "${CARD_NAME} needs TinyUSB 0.21 or later, but ${_tinyusb_found} in ${_tinyusb_dir}. Configure with -DPICO_TINYUSB_PATH=/path/to/tinyusb to build it (use an absolute path or \$HOME; a ~ is not expanded inside -D options).")
endif()

add_executable(${CARD_NAME}
	${CMAKE_CURRENT_LIST_DIR}/main.cpp
	${CMAKE_CURRENT_LIST_DIR}/diskio.c
	${CMAKE_CURRENT_LIST_DIR}/fatfs/ff.c
	${CMAKE_CURRENT_LIST_DIR}/fatfs/ffunicode.c
)

target_compile_options(${CARD_NAME} PRIVATE
	-Wdouble-promotion -Wfloat-conversion -Wall -Wextra -O2)

target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# REQUIRED for the Workshop Computer's crystal. Without it the card fails to
# boot on a cold power-up (it works from a warm reset, which is what makes the
# bug so confusing). Do not remove.
target_compile_definitions(${CARD_NAME} PRIVATE
	PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

# The local tusb_config.h is picked up by TinyUSB (its default
# CFG_TUSB_CONFIG_FILE is "tusb_config.h") through the include path below.
target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(${CARD_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/fatfs)

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

# Run everything from RAM. Core 0's USB/FatFs activity would otherwise evict
# core 1's audio code from the shared XIP cache and upset ComputerCard's ADC
# timing; the card is small enough for this to be affordable.
pico_set_binary_type(${CARD_NAME} copy_to_ram)

pico_add_extra_outputs(${CARD_NAME})

# The front jack is needed for USB *host* duty, so there is no USB serial
# console. Enabling stdio over USB would fight the host stack for the
# controller, and UART stdio would clash with the normalisation probe.
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
