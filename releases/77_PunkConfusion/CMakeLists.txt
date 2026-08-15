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

cmake_minimum_required(VERSION 3.13)

# The shared ComputerCard import helper consults PICO_SDK_PATH before it can
# fetch or discover anything else. Ignore stale shell values so this CMake file
# works both inside Workshop_Computer/releases and in the standalone repo.
if(DEFINED ENV{PICO_SDK_PATH} AND NOT EXISTS "$ENV{PICO_SDK_PATH}/pico_sdk_init.cmake")
  unset(ENV{PICO_SDK_PATH})
endif()

set(PICO_SDK_CANDIDATES
  ${CMAKE_CURRENT_LIST_DIR}/../pico-sdk
  ${CMAKE_CURRENT_LIST_DIR}/../pico-sdk-src
  ${USERHOME}/pico-sdk
  /opt/pico-sdk)

if(NOT DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
  foreach(candidate ${PICO_SDK_CANDIDATES})
    if(EXISTS ${candidate}/pico_sdk_init.cmake)
      set(PICO_SDK_PATH ${candidate} CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
      break()
    endif()
  endforeach()
endif()

if(NOT DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
  set(PICO_SDK_FETCH_FROM_GIT ON CACHE BOOL "Fetch Raspberry Pi Pico SDK when no local SDK is available" FORCE)
  set(PICO_SDK_FETCH_FROM_GIT_TAG ${sdkVersion} CACHE STRING "Raspberry Pi Pico SDK git tag" FORCE)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)

set(CARD_NAME punk_confusion)
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(${CARD_NAME}
  main.cpp)

target_include_directories(${CARD_NAME} PUBLIC
  ${CMAKE_CURRENT_LIST_DIR})

# The card-local ComputerCard.h is intentionally searched first. It keeps the
# newer per-card hardware fixes without modifying the upstream HelloWorlds copy.

target_compile_options(${CARD_NAME} PRIVATE
  -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)

target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_link_libraries(${CARD_NAME}
  pico_stdlib
  hardware_dma
  hardware_i2c
  hardware_pwm
  hardware_adc
  hardware_spi
  hardware_vreg
  hardware_flash)

# Real embedded vocal samples are too large for copy_to_ram; keep code/sample
# data in flash so RAM remains available for delay buffers and audio state.
pico_set_binary_type(${CARD_NAME} default)
pico_add_extra_outputs(${CARD_NAME})
pico_enable_stdio_usb(${CARD_NAME} 0)
