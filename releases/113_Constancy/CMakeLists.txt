cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(Constancy C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/lib
)

# Warnings from the ComputerCard examples: -Wdouble-promotion and
# -Wfloat-conversion catch accidental (very slow) double-precision maths.
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wdouble-promotion -Wfloat-conversion -Wall -Wextra
)

# Print RAM use on every build -- the delay line takes about a quarter of
# the RP2040's 264KB, so this is the number to watch.
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--print-memory-usage)

# Run everything from RAM: no flash-cache misses stalling the audio
# interrupt, and the card never touches the flash after boot.
pico_set_binary_type(${PROJECT_NAME} copy_to_ram)

# PICO_XOSC_STARTUP_DELAY_MULTIPLIER: without it the card works on first
# flash but fails after a reset -- the crystal needs longer to start.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_link_libraries(${PROJECT_NAME}
    pico_unique_id
    pico_stdlib
    pico_rand
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    hardware_clocks
    hardware_vreg
    pico_multicore
)

# USB stdio off: it interferes with the normalisation probe.
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})

# Cycle-counting build for tools/bench/cycles.py (not for flashing): exports
# ComputerCard's field offsets and core 1's update so an emulator can drive
# ProcessSample directly.
option(CONSTANCY_BENCH "Build for the cycle-counting emulator" OFF)
if (CONSTANCY_BENCH)
    target_sources(${PROJECT_NAME} PRIVATE tools/bench/offsets.cpp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CONSTANCY_BENCH=1)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--undefined=bench_offsets -Wl,--undefined=bench_control_tick -Wl,--undefined=bench_seed)
endif()
