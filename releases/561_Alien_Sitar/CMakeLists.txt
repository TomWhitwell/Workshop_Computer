cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD pico CACHE STRING "Workshop Computer uses RP2040")
set(PICO_NO_PICOTOOL 1 CACHE BOOL "Use validated local RP2040 UF2 writer")
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(alien_sitar C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
add_executable(alien_sitar
    firmware/main.cpp firmware/audio_backend.cpp
    dsp/fixed.cpp dsp/filter.cpp dsp/modal.cpp dsp/voice.cpp
    port/controller.cpp port/events.cpp port/envelopes.cpp port/output.cpp
    port/processor.cpp port/auxiliary.cpp port/bloom.cpp
)
target_include_directories(alien_sitar PRIVATE vendor/ComputerCard)
# See scripts/build.sh --help for prerequisites and the build command.
target_compile_options(alien_sitar PRIVATE
    -O3 -Wall -Wextra -Wdouble-promotion -Wfloat-conversion
    -fstack-usage -Wstack-usage=1024 -Wno-unused-parameter
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
)
target_compile_definitions(alien_sitar PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    PICO_USE_STACK_GUARDS=1
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x1000
)
target_link_libraries(alien_sitar
    pico_stdlib pico_multicore pico_unique_id hardware_dma hardware_i2c
    hardware_pwm hardware_adc hardware_spi
)
# Card construction finishes before launching core1; no concurrent local-static initialisation.
# Both cores execute from SRAM to avoid XIP cache contention in the hot paths.
pico_set_binary_type(alien_sitar copy_to_ram)
pico_enable_stdio_usb(alien_sitar 0)
pico_enable_stdio_uart(alien_sitar 0)
target_link_options(alien_sitar PRIVATE -Wl,--print-memory-usage)
pico_add_extra_outputs(alien_sitar)
