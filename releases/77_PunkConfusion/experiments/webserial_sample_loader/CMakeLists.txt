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

project(punk_confusion_webserial_sample_loader C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

function(add_punk_confusion_target TARGET_NAME FLASH_BYTES SAMPLE_BANK_BYTES)
add_executable(${TARGET_NAME}
  main.cpp)

target_include_directories(${TARGET_NAME} PUBLIC
  ${CMAKE_CURRENT_LIST_DIR})

# The card-local ComputerCard.h is intentionally searched first. It keeps the
# newer per-card hardware fixes without modifying the upstream HelloWorlds copy.

target_compile_options(${TARGET_NAME} PRIVATE
  -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)

target_link_options(${TARGET_NAME} PRIVATE -Wl,--print-memory-usage)

target_compile_definitions(${TARGET_NAME} PRIVATE
  PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
  PICO_FLASH_SIZE_BYTES=${FLASH_BYTES}
  PUNK_SAMPLE_BANK_SIZE_BYTES=${SAMPLE_BANK_BYTES})

target_link_libraries(${TARGET_NAME}
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
pico_set_binary_type(${TARGET_NAME} default)

set(LINKER_TEMPLATE ${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2040/memmap_default.ld)
set(LINKER_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_memmap_default.ld)
file(READ ${LINKER_TEMPLATE} LINKER_TEXT)
string(REPLACE
  "    INCLUDE \"pico_flash_region.ld\""
  "    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = ${FLASH_BYTES}"
  LINKER_TEXT
  "${LINKER_TEXT}")
file(WRITE ${LINKER_SCRIPT} "${LINKER_TEXT}")
pico_set_linker_script(${TARGET_NAME} ${LINKER_SCRIPT})

pico_add_extra_outputs(${TARGET_NAME})
pico_enable_stdio_usb(${TARGET_NAME} 1)
pico_enable_stdio_uart(${TARGET_NAME} 0)
endfunction()

add_punk_confusion_target(punk_confusion_2mb 2097152 1048576)
add_punk_confusion_target(punk_confusion_16mb 16777216 14680064)
