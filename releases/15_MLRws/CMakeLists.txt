cmake_minimum_required(VERSION 3.13)

## -----------------------------------------------------------------------------
## User settings (edit these, or pass -D... to CMake)
## -----------------------------------------------------------------------------

# Name of the firmware/UF2 produced.
set(CARD_NAME "MLRws" CACHE STRING "ComputerCard firmware name (also the UF2 filename stem)")
set(MLR_FIRMWARE_VERSION "1.1.5" CACHE STRING "MLRws firmware version reported to the sample-manager web app")

# USB serial stdio — OFF for this card (USB port is used for host mode).
set(CARD_STDIO_USB OFF CACHE BOOL "Enable USB stdio")

# Pico SDK import
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake")
	include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
else()
	if (NOT DEFINED PICO_SDK_PATH)
		message(FATAL_ERROR "PICO_SDK_PATH not set and pico_sdk_import.cmake not found.")
	endif()
	include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

# MLRws runs from RAM while flash writes are active; -O3 grows RAM-resident code
# enough to leave very little headroom, while -O2 has tested without overruns.
foreach(LANG C CXX)
	if(CMAKE_${LANG}_FLAGS_RELEASE MATCHES "(^| )-O3( |$)")
		string(REPLACE "-O3" "-O2" CMAKE_${LANG}_FLAGS_RELEASE "${CMAKE_${LANG}_FLAGS_RELEASE}")
		set(CMAKE_${LANG}_FLAGS_RELEASE "${CMAKE_${LANG}_FLAGS_RELEASE}" CACHE STRING "Flags used by the ${LANG} compiler during Release builds." FORCE)
	endif()
endforeach()

set(MLR_FLASH_TOTAL_BYTES 2097152 CACHE STRING "Total flash size of the program card" FORCE)
set(MLR_FIRMWARE_RESERVE_BYTES 163840 CACHE STRING "Flash bytes reserved for firmware" FORCE)
set(MLR_TRACK_FLASH_SIZE_BYTES 319488 CACHE STRING "Flash bytes reserved per track, including the 4 KB header" FORCE)
set(MLR_SCENE_SECTORS_COUNT 3 CACHE STRING "Flash sectors reserved for scene persistence" FORCE)
set(MLR_FLASH_SECTOR_BYTES 4096 CACHE STRING "Flash erase sector size in bytes" FORCE)

foreach(ALIGN_VALUE MLR_FIRMWARE_RESERVE_BYTES MLR_TRACK_FLASH_SIZE_BYTES)
	math(EXPR _align_rem "${${ALIGN_VALUE}} % ${MLR_FLASH_SECTOR_BYTES}")
	if(NOT _align_rem EQUAL 0)
		message(FATAL_ERROR "${ALIGN_VALUE} must be a multiple of ${MLR_FLASH_SECTOR_BYTES} bytes")
	endif()
endforeach()

math(EXPR MLR_LAYOUT_BYTES
	"${MLR_FIRMWARE_RESERVE_BYTES} + (6 * ${MLR_TRACK_FLASH_SIZE_BYTES}) + (${MLR_SCENE_SECTORS_COUNT} * ${MLR_FLASH_SECTOR_BYTES})")
math(EXPR MLR_LAYOUT_HEADROOM "${MLR_FLASH_TOTAL_BYTES} - ${MLR_LAYOUT_BYTES}")
if(MLR_LAYOUT_BYTES GREATER MLR_FLASH_TOTAL_BYTES)
	message(FATAL_ERROR
		"Flash layout exceeds card capacity: layout=${MLR_LAYOUT_BYTES} bytes, flash=${MLR_FLASH_TOTAL_BYTES} bytes")
endif()

message(STATUS
	"MLR flash layout: firmware reserve=${MLR_FIRMWARE_RESERVE_BYTES} bytes, track size=${MLR_TRACK_FLASH_SIZE_BYTES} bytes, tail headroom=${MLR_LAYOUT_HEADROOM} bytes")

pico_sdk_init()

add_executable(${CARD_NAME}
	${CMAKE_CURRENT_LIST_DIR}/main.cpp
	${CMAKE_CURRENT_LIST_DIR}/monome_ws.c
	${CMAKE_CURRENT_LIST_DIR}/mlr.c
	${CMAKE_CURRENT_LIST_DIR}/device_mode.c
	${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
)

target_compile_options(${CARD_NAME} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

option(MLR_PERF_PROFILING "Enable debugger-readable performance counters" OFF)
option(MONOME_WS_FORCE_8X8_MONOBRIGHT "Force modern mext grids to behave like an 8x8 binary monobright grid for testing" OFF)
option(CARD_STEREO "Deprecated; ignored because MLRws now builds one dual-mono firmware" OFF)
mark_as_advanced(CARD_STEREO)

# Run all code from RAM so flash writes don't stall core 0.
pico_set_binary_type(${CARD_NAME} copy_to_ram)
target_compile_definitions(${CARD_NAME} PRIVATE
	PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
	MLR_FIRMWARE_VERSION="${MLR_FIRMWARE_VERSION}"
	MLR_FIRMWARE_RESERVE=${MLR_FIRMWARE_RESERVE_BYTES}
	MLR_TRACK_FLASH_SIZE=${MLR_TRACK_FLASH_SIZE_BYTES}
	MLR_SCENE_SECTORS=${MLR_SCENE_SECTORS_COUNT}
	MLR_SECTOR_SIZE=${MLR_FLASH_SECTOR_BYTES}
	$<$<BOOL:${MONOME_WS_FORCE_8X8_MONOBRIGHT}>:MONOME_WS_FORCE_8X8_MONOBRIGHT=1>
	$<$<BOOL:${MLR_PERF_PROFILING}>:MLR_PERF_PROFILING>
)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${CARD_NAME}
	pico_stdlib
	pico_multicore
	hardware_adc
	hardware_clocks
	hardware_dma
	hardware_flash
	hardware_gpio
	hardware_i2c
	hardware_irq
	hardware_pwm
	hardware_spi
	tinyusb_host
	tinyusb_device
	tinyusb_board
)

pico_add_extra_outputs(${CARD_NAME})

if (CARD_STDIO_USB)
	pico_enable_stdio_usb(${CARD_NAME} 1)
else()
	pico_enable_stdio_usb(${CARD_NAME} 0)
endif()

# Collect generated UF2 files in one place (in addition to the dev-env MAKEFILES helper).
set(COMPUTERCARD_UF2_DIR "${CMAKE_CURRENT_LIST_DIR}/UF2" CACHE PATH "Directory to copy generated .uf2 files into")
set(MLR_VERSIONED_UF2_NAME "${CARD_NAME}.${MLR_FIRMWARE_VERSION}.uf2")
add_custom_command(
	TARGET ${CARD_NAME}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND}
		-DBIN_FILE="$<TARGET_FILE_DIR:${CARD_NAME}>/${CARD_NAME}.bin"
		-DFIRMWARE_RESERVE=${MLR_FIRMWARE_RESERVE_BYTES}
		-P "${CMAKE_CURRENT_LIST_DIR}/cmake/check_firmware_reserve.cmake"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${COMPUTERCARD_UF2_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE_DIR:${CARD_NAME}>/${CARD_NAME}.uf2"
		"${COMPUTERCARD_UF2_DIR}/${MLR_VERSIONED_UF2_NAME}"
)
