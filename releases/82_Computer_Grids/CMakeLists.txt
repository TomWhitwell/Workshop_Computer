cmake_minimum_required(VERSION 3.13)

# Explicit board matches other Workshop_Computer releases (55_fifths, 22_sheep, etc.).
# Ensures UF2/link settings match what `make build` expects via PICO_BOARD_HEADER_DIRS.
set(PICO_BOARD pico CACHE STRING "Board type")

## -----------------------------------------------------------------------------
## User settings (edit these, or pass -D... to CMake)
## -----------------------------------------------------------------------------

# Name of the firmware/UF2 produced.
# This becomes the CMake target name and the resulting UF2 name: <CARD_NAME>.uf2
set(CARD_NAME "82_Computer_Grids" CACHE STRING "ComputerCard firmware name (also the UF2 filename stem)")

# USB serial stdio (usually OFF for audio-rate cards; set ON for debug).
set(CARD_STDIO_USB OFF CACHE BOOL "Enable USB stdio")

# Pico SDK import
# - Prefer a local copy of pico_sdk_import.cmake if present.
# - Otherwise require PICO_SDK_PATH (provided by this dev container).
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake")
	include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
else()
	if (NOT DEFINED PICO_SDK_PATH)
		message(FATAL_ERROR "PICO_SDK_PATH not set and pico_sdk_import.cmake not found. Set PICO_SDK_PATH or add pico_sdk_import.cmake to this directory.")
	endif()
	include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(${CARD_NAME}
	${CMAKE_CURRENT_LIST_DIR}/main.cpp
	${CMAKE_CURRENT_LIST_DIR}/ComputerCardImpl.cpp
	${CMAKE_CURRENT_LIST_DIR}/GridsCard.cpp
	${CMAKE_CURRENT_LIST_DIR}/ConfigStore.cpp
	${CMAKE_CURRENT_LIST_DIR}/GridsEngine.cpp
	${CMAKE_CURRENT_LIST_DIR}/GridsResources.cpp
	${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
)
# Run from flash like 20_reverb (avoid copy_to_ram + USB/core interaction surprises).

target_compile_options(${CARD_NAME} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# Workshop 20_reverb: rhport0 device|host in tusb_config.h; runtime tud_init vs tuh_init.
target_compile_definitions(${CARD_NAME} PRIVATE
	PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
	CFG_TUSB_MCU=OPT_MCU_RP2040
	CFG_TUD_ENABLED=1
	CFG_TUH_ENABLED=1
	CFG_TUD_MIDI=1
)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${CARD_NAME}
	pico_stdlib
	pico_multicore
	pico_unique_id
	pico_platform
	hardware_adc
	hardware_clocks
	hardware_dma
	hardware_flash
	hardware_gpio
	hardware_i2c
	hardware_irq
	hardware_pwm
	hardware_spi
	hardware_sync
	tinyusb_device
	tinyusb_host
	tinyusb_board
)

pico_add_extra_outputs(${CARD_NAME})

# USB stdio needs CDC in TinyUSB; this firmware is MIDI-only (see usb_descriptors.c).
# Do not link pico_stdio_usb here — without CDC the SDK emits #warning and it adds no benefit.
if (CARD_STDIO_USB)
	message(WARNING "CARD_STDIO_USB is ignored for this MIDI-only build (no USB CDC).")
endif()
pico_enable_stdio_usb(${CARD_NAME} 0)
pico_enable_stdio_uart(${CARD_NAME} 0)

# Collect generated UF2 files in one place (in addition to the dev-env MAKEFILES helper).
# Use CMAKE_CURRENT_BINARY_DIR like 55_fifths / 22_sheep (same path picotool writes for this target).
set(COMPUTERCARD_UF2_DIR "${CMAKE_CURRENT_LIST_DIR}/UF2" CACHE PATH "Directory to copy generated .uf2 files into")
add_custom_command(
	TARGET ${CARD_NAME}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${COMPUTERCARD_UF2_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy
		"${CMAKE_CURRENT_BINARY_DIR}/${CARD_NAME}.uf2"
		"${COMPUTERCARD_UF2_DIR}/${CARD_NAME}.uf2"
)
