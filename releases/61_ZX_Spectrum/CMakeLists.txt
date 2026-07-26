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
cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)

set(CARD_NAME "zx")
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# --- Generate ROM headers from roms/*.rom (Amstrad-permitted Spectrum ROMs) ---
# Derived headers are git-ignored and rebuilt here. See reference/ROMS.md.
find_package(Python3 COMPONENTS Interpreter)
set(ROM_HEADERS
  ${CMAKE_CURRENT_LIST_DIR}/rom128_0.h
  ${CMAKE_CURRENT_LIST_DIR}/rom128_1.h
  ${CMAKE_CURRENT_LIST_DIR}/rom48.h)
if (Python3_FOUND)
  add_custom_command(
    OUTPUT ${ROM_HEADERS}
    COMMAND ${Python3_EXECUTABLE} tools/bin2h.py roms/spec128uk.rom rom128_0 rom128_0.h --slice 0x0000 0x4000
    COMMAND ${Python3_EXECUTABLE} tools/bin2h.py roms/spec128uk.rom rom128_1 rom128_1.h --slice 0x4000 0x4000
    COMMAND ${Python3_EXECUTABLE} tools/bin2h.py roms/spec48.rom    rom48    rom48.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    DEPENDS roms/spec128uk.rom roms/spec48.rom tools/bin2h.py
    COMMENT "Generating ROM headers from roms/*.rom")
  add_custom_target(rom_headers DEPENDS ${ROM_HEADERS})
elseif (NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/rom128_0.h)
  message(FATAL_ERROR "Python3 not found and ROM headers not generated. "
    "Run tools/bin2h.py manually (see reference/ROMS.md).")
endif()

# Baked-in default snapshot: generate snapshot_data.h from the committed demo
# (snapshots/bakedasm.z80, Andy's freely-usable ASM). If no demo is present, the
# __has_include fallback in snapshot_default.h boots to the bare 128K menu.
set(DEFAULT_SNAPSHOT ${CMAKE_CURRENT_LIST_DIR}/snapshots/bakedasm.z80)
if (Python3_FOUND AND EXISTS ${DEFAULT_SNAPSHOT})
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_LIST_DIR}/snapshot_data.h
    COMMAND ${Python3_EXECUTABLE} tools/bin2h.py snapshots/bakedasm.z80 snapshot_z80 snapshot_data.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    DEPENDS ${DEFAULT_SNAPSHOT} tools/bin2h.py
    COMMENT "Generating snapshot_data.h from snapshots/bakedasm.z80")
  add_custom_target(snapshot_header DEPENDS ${CMAKE_CURRENT_LIST_DIR}/snapshot_data.h)
endif()

# Set -DDIAG=ON to build the minimal bisecting diagnostic instead of the emulator.
option(DIAG "Build diagnostic firmware (diag.cpp) instead of the emulator" OFF)
if (DIAG)
  add_executable(${CARD_NAME}
    diag.cpp
    machine.cpp
    spectrum.cpp
    ay.cpp
    snapshot.cpp
    mapping.cpp
    vendor/sz80/z80.c)
else()
  add_executable(${CARD_NAME}
    main.cpp
    machine.cpp
    spectrum.cpp
    ay.cpp
    snapshot.cpp
    mapping.cpp
    reverb.cpp
    webui.cpp
    onebit.cpp
    usb_descriptors.c
    vendor/sz80/z80.c)
endif()
if (TARGET rom_headers)
  add_dependencies(${CARD_NAME} rom_headers)
endif()
if (TARGET snapshot_header)
  add_dependencies(${CARD_NAME} snapshot_header)
endif()
target_compile_options(${CARD_NAME} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
# NOTE: copy_to_ram was needed for the old cycle-stepped core. The instruction-
# stepped superzazu core does far fewer flash fetches, so we run from flash (XIP)
# to reclaim ~90KB of RAM for AY/Web-UI/buffers. If this regresses speed, the fix
# is to __not_in_flash_func just the z80 hot functions, not copy the whole image.
# pico_set_binary_type(${CARD_NAME} copy_to_ram)
# Project root (ComputerCard.h, spectrum.h, generated rom*.h) + vendored z80.h.
target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/vendor/sz80)
target_link_libraries(${CARD_NAME} pico_unique_id pico_stdlib pico_multicore hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_vreg tinyusb_device tinyusb_board)
pico_add_extra_outputs(${CARD_NAME})
pico_enable_stdio_usb(${CARD_NAME} 0)
