cmake_minimum_required(VERSION 3.13)

# Explicit board matches other Workshop_Computer releases.
set(PICO_BOARD pico CACHE STRING "Board type")

if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake")
	include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
else()
	if (NOT DEFINED PICO_SDK_PATH)
		message(FATAL_ERROR "PICO_SDK_PATH not set and pico_sdk_import.cmake not found.")
	endif()
	include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

set(CARD_NAME "92_usb_midi_host_synth" CACHE STRING "ComputerCard firmware name (also the UF2 filename stem)")

project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(CARD_ROOT ${CMAKE_CURRENT_LIST_DIR})

add_executable(${CARD_NAME}
	${CARD_ROOT}/main.cpp
	${CARD_ROOT}/ComputerCardImpl.cpp
	${CARD_ROOT}/app/card.cpp
	${CARD_ROOT}/app/panel_setup.cpp
	${CARD_ROOT}/app/runtime_state.cpp
	${CARD_ROOT}/app/config_store.cpp
	${CARD_ROOT}/app/glyph_leds.cpp
	${CARD_ROOT}/app/param_maps.cpp
	${CARD_ROOT}/app/midi_parser.cpp
	${CARD_ROOT}/app/sysex_editor.cpp
	${CARD_ROOT}/dsp/adsr.cpp
	${CARD_ROOT}/dsp/voice_matrix.cpp
	${CARD_ROOT}/dsp/synth.cpp
	${CARD_ROOT}/dsp/voices.cpp
	${CARD_ROOT}/dsp/drums.cpp
	${CARD_ROOT}/usb/usb_role.cpp
	${CARD_ROOT}/usb/usb_core1.cpp
	${CARD_ROOT}/usb/usb_device_midi.cpp
	${CARD_ROOT}/usb/usb_descriptors.c
	${CARD_ROOT}/usb/usb_midi_host.c
	${CARD_ROOT}/usb/usb_midi_host_app_driver.c
)

# Run from flash (avoid copy_to_ram + USB/core interaction surprises; see 82_Computer_Grids).
target_include_directories(${CARD_NAME} PUBLIC
	${CARD_ROOT}
	${CARD_ROOT}/app
	${CARD_ROOT}/dsp
	${CARD_ROOT}/usb
)

target_compile_definitions(${CARD_NAME} PRIVATE
	PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
	CFG_TUSB_MCU=OPT_MCU_RP2040
	CFG_TUD_ENABLED=1
	CFG_TUH_ENABLED=1
	CFG_TUD_MIDI=1
)

target_compile_options(${CARD_NAME} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

target_link_libraries(${CARD_NAME}
	tinyusb_device
	tinyusb_host
	tinyusb_board
	pico_unique_id
	pico_stdlib
	pico_multicore
	hardware_dma
	hardware_i2c
	hardware_pwm
	hardware_adc
	hardware_spi
	hardware_flash
	hardware_sync
)

pico_enable_stdio_uart(${CARD_NAME} 0)
pico_enable_stdio_usb(${CARD_NAME} 0)
pico_add_extra_outputs(${CARD_NAME})

set(COMPUTERCARD_UF2_DIR "${CARD_ROOT}/UF2" CACHE PATH "Directory to copy generated .uf2 files into")
add_custom_command(
	TARGET ${CARD_NAME}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${COMPUTERCARD_UF2_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy
		"${CMAKE_CURRENT_BINARY_DIR}/${CARD_NAME}.uf2"
		"${COMPUTERCARD_UF2_DIR}/${CARD_NAME}.uf2"
)
