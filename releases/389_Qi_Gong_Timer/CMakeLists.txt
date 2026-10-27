# QiGong Timer: CMake for a Workshop Computer program card.
# Included: lib/ComputerCard.h (v0.4.0, MIT, Chris Johnson) and pico_sdk_import.cmake (from pico-sdk 2.1.1).
# (ComputerCard.h lives in lib/ so the PC tests pick up their mock instead: see host/mock/.)
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake")
  message(FATAL_ERROR "pico_sdk_import.cmake is missing: copy it from pico-sdk/external/ into this folder.")
endif()
include(pico_sdk_import.cmake)
project(qi_timer C CXX ASM)
pico_sdk_init()

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/lib/ComputerCard.h")
  message(FATAL_ERROR "ComputerCard.h is missing: get v0.4.0 from Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ in the Workshop_Computer repo")
endif()

add_executable(qi_timer main.cpp)
target_include_directories(qi_timer PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/lib)

# Clock: 144 MHz by default (the directive's default; 192 MHz whined on CV Out 1 on the owner's unit).
option(CARD_192MHZ "Run at 192 MHz / 1.15 V instead of 144 MHz (option only)" OFF)
if(CARD_192MHZ)
  target_compile_definitions(qi_timer PRIVATE CARD_192MHZ=1)
  target_link_libraries(qi_timer hardware_vreg)
endif()
# Silence keeper: by default it also gates the DRY signal between sounds (the owner's choice: true silence between alerts).
option(CARD_KEEPER_SEND_ONLY "Silence keeper gates only the effect send; the dry signal is never touched" OFF)
if(CARD_KEEPER_SEND_ONLY)
  target_compile_definitions(qi_timer PRIVATE QI_KEEPER_SEND_ONLY=1)
endif()
option(CARD_BENCH "Bench build for scripts/bench_cycles.py (NOT a card firmware)" OFF)
if(CARD_BENCH)
  target_compile_definitions(qi_timer PRIVATE CARD_BENCH=1)
  # The emulator has no boot ROM, where the SDK's float routines normally live (floats are only used at start-up)
  pico_set_float_implementation(qi_timer compiler)
  pico_set_double_implementation(qi_timer compiler)
endif()
option(CARD_PROFILE "Toggle GPIO 0 around ProcessSample (for scope timing)" OFF)
if(CARD_PROFILE)
  target_compile_definitions(qi_timer PRIVATE CARD_PROFILE=1)
endif()

target_compile_options(qi_timer PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(qi_timer PRIVATE -Wl,--print-memory-usage)      # prints the RAM/flash table: check RAM < 100%
target_compile_definitions(qi_timer PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)   # works after a reset, not just on first flash
target_link_libraries(qi_timer pico_unique_id pico_stdlib pico_multicore hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi)
pico_enable_stdio_usb(qi_timer 0)            # USB serial interferes with the normalisation probe
pico_set_binary_type(qi_timer copy_to_ram)   # run from RAM: no flash-cache jitter (the code then also uses RAM)
pico_add_extra_outputs(qi_timer)             # produces qi_timer.uf2
