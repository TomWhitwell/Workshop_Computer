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

# The shared ComputerCard import helper consults the shell environment first.
# If the current shell is carrying a stale PICO_SDK_PATH, clear it here so this
# card can still build via /opt/pico-sdk or FetchContent.
if(DEFINED ENV{PICO_SDK_PATH} AND NOT EXISTS "$ENV{PICO_SDK_PATH}")
  set(ENV{PICO_SDK_PATH} "")
endif()

include(../../Demonstrations+HelloWorlds/PicoSDK/ComputerCard/pico_sdk_import.cmake)

set(CARD_NAME punk_confusion)
project(${CARD_NAME} C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(${CARD_NAME}
  main.cpp)

target_include_directories(${CARD_NAME} PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../../Demonstrations+HelloWorlds/PicoSDK/ComputerCard)

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

pico_set_binary_type(${CARD_NAME} copy_to_ram)
pico_add_extra_outputs(${CARD_NAME})
pico_enable_stdio_usb(${CARD_NAME} 0)
