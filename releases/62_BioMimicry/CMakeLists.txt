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

set(CARD_NAME "biomimicry")
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# --- Optional baked-in PCM samples ---------------------------------------
# tools/mksamples.py writes samples.h from samples/*.raw (8-bit signed mono
# 48kHz). Neither the .raw files nor the generated header are committed: with no
# samples present the build still succeeds and alt-boot falls back to the
# synthesized voices (see samples_default.h).
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
  # The generated header is included by voices.cpp (via samples_default.h);
  # name it as a source dependency so editing a sample actually relinks.
  set_source_files_properties(voices.cpp PROPERTIES
    OBJECT_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/samples.h)
endif()

add_executable(${CARD_NAME}
  main.cpp
  engines.cpp
  voices.cpp
  fastmath.cpp
  webui.cpp
  usb_descriptors.c)

if (TARGET pcm_samples)
  add_dependencies(${CARD_NAME} pcm_samples)
endif()

# Cycle-count ProcessSample and show the worst case on the LEDs. Build into a
# SEPARATE directory so the released firmware is never a profile build:
#   cmake -B build-profile -G Ninja -DBIO_PROFILE=ON
option(BIO_PROFILE "Cycle-count ProcessSample and report on the LEDs" OFF)
if (BIO_PROFILE)
  target_compile_definitions(${CARD_NAME} PRIVATE BIO_PROFILE=1)
  message(STATUS "BIO_PROFILE on - this build reports timing, do not release it")
endif()

target_compile_options(${CARD_NAME} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)
# The Workshop Computer's crystal needs a long startup delay before the PLL locks.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

# Project root, so #include "ComputerCard.h" and the card's own headers resolve.
target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(${CARD_NAME}
  pico_unique_id pico_stdlib pico_multicore
  hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_vreg
  hardware_flash tinyusb_device tinyusb_board)

# The last 1MB of flash is reserved for user samples uploaded over USB (see
# samplestore.h). Keep the firmware image out of it.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_FLASH_SIZE_BYTES=2097152)

# Guard the boundary. The firmware image (code + baked samples) must not reach
# 0x100000, or flashing it would overwrite uploaded user samples and an upload
# would overwrite the firmware. Baked audio is the thing most likely to push it
# over, so fail the build loudly rather than corrupt a card.
add_custom_command(TARGET ${CARD_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DELF=$<TARGET_FILE:${CARD_NAME}>
    -DOBJDUMP=${CMAKE_OBJDUMP}
    -DLIMIT=1048576
    -P ${CMAKE_CURRENT_LIST_DIR}/tools/checksize.cmake)
pico_add_extra_outputs(${CARD_NAME})
pico_enable_stdio_usb(${CARD_NAME} 0)
