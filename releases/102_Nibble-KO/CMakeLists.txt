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

set(CARD_NAME "nibbleko")
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# --- Optional baked-in PCM samples ---------------------------------------
# tools/mksamples.py writes samples.h from samples/*.raw (8-bit signed mono
# 48kHz, one file per voice slot — see tools/importwav.py). Neither the .raw
# files nor the generated header are committed: with no samples present the
# build still succeeds and every voice has nothing baked to fall back on
# (see samples_default.h). Ported from WorkshopBio/CMakeLists.txt.
find_package(Python3 COMPONENTS Interpreter)
set(SAMPLE_DIR ${CMAKE_CURRENT_LIST_DIR}/samples)
if (Python3_FOUND AND EXISTS ${SAMPLE_DIR})
  file(GLOB SAMPLE_FILES ${SAMPLE_DIR}/*.raw)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_LIST_DIR}/samples.h
    COMMAND ${Python3_EXECUTABLE} tools/mksamples.py samples samples.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    DEPENDS tools/mksamples.py ${SAMPLE_FILES}
    COMMENT "Generating samples.h from samples/*.raw")
  add_custom_target(pcm_samples DEPENDS ${CMAKE_CURRENT_LIST_DIR}/samples.h)
endif()

add_executable(${CARD_NAME}
  main.cpp
  levels.cpp
  drums.cpp
  looper.cpp
  fx.cpp
  fastmath.cpp
  webui.cpp
  usb_descriptors.c)

if (TARGET pcm_samples)
  add_dependencies(${CARD_NAME} pcm_samples)
endif()
# drums.cpp reaches samples.h through samplestore.h -> samples_default.h.
# CMake cannot see that through a generated header, so name it explicitly:
# without this, re-importing a sample regenerates samples.h and nothing
# rebuilds, and you flash firmware that still contains the old audio.
if (TARGET pcm_samples)
  set_source_files_properties(drums.cpp PROPERTIES
    OBJECT_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/samples.h)
endif()

target_compile_options(${CARD_NAME} PRIVATE
  -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)

# Worth watching every build: a sudden jump means something large landed in
# the wrong section.
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# REQUIRED for the Workshop Computer's crystal. Without it the card fails to
# boot on a cold power-up (it works from a warm reset, which is exactly what
# makes the bug so confusing). Do not remove.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# USB lives on core 1, which is why pico_multicore is here. NIBBLE (the
# synth-only sibling) needs neither: its whole per-sample load is a few
# hundred cycles and it has nothing to talk to. TinyUSB's tud_task() measured
# ~36000 cycles on WorkshopBio, i.e. 14x the entire 20.8us audio budget, so it
# cannot share core 0 with ProcessSample() — see main.cpp's core1Entry.
#
# hardware_flash would be needed even if this card stored nothing:
# ComputerCard.h includes hardware/flash.h to read the factory CV output
# calibration. It now also carries the saved levels (calibstore.h) and user
# samples (samplestore.h).
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
  tinyusb_device
  tinyusb_board)

# Flash is carved up in three: firmware low, the saved calibration at 512KB
# (calibstore.h), user samples in the top 1MB (samplestore.h). Both stored
# regions sit at FIXED addresses so reflashing the firmware never moves or
# wipes them.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_FLASH_SIZE_BYTES=2097152)

# Guard the first boundary the image can actually hit. The firmware (code plus
# baked samples) must not reach the calibration sector at 512KB, or flashing
# would destroy the saved levels and the card would come up needing a full
# recalibration. Baked audio is the thing most likely to push it over, so fail
# the build loudly rather than corrupt a card.
#
# -DLIMIT tracks calibstore.h's kCalibRegionOff. If that moves, move this.
add_custom_command(TARGET ${CARD_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DELF=$<TARGET_FILE:${CARD_NAME}>
    -DOBJDUMP=${CMAKE_OBJDUMP}
    -DLIMIT=524288
    -P ${CMAKE_CURRENT_LIST_DIR}/tools/checksize.cmake)

pico_add_extra_outputs(${CARD_NAME})

# The Computer has no USB serial console in normal use, and enabling stdio
# over USB pulls in the whole TinyUSB stack.
pico_enable_stdio_usb(${CARD_NAME} 0)
