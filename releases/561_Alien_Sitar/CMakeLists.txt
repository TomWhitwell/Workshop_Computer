cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD pico CACHE STRING "Computer uses RP2040, not RP2350")
set(PICO_NO_PICOTOOL 1 CACHE BOOL "Use validated local RP2040 UF2 writer")
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(mtm_sitar C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(TINYUSB_OPT_OS OPT_OS_NONE)
pico_sdk_init()
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/core_sources.txt" MTM_CORE_SOURCES REGEX "^[^#].*cpp$")
add_executable(mtm_sitar firmware/main.cpp firmware/audio_backend.cpp firmware/storage.cpp ${MTM_CORE_SOURCES})
target_include_directories(mtm_sitar PRIVATE vendor/ComputerCard)
# Shared source list also drives the desktop harness and instruction profiler.
target_compile_options(mtm_sitar PRIVATE
    -O3 -Wall -Wextra -Wdouble-promotion -Wfloat-conversion
    -fstack-usage -Wstack-usage=1024 -Wno-unused-parameter
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
)
target_compile_definitions(mtm_sitar PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    PICO_USE_STACK_GUARDS=1
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x1000
)
target_link_libraries(mtm_sitar
    pico_stdlib pico_multicore pico_unique_id hardware_dma hardware_i2c
    hardware_pwm hardware_adc hardware_spi
)
# Card construction finishes before launching core1; no concurrent local-static initialisation.
# Both cores execute from SRAM to avoid XIP cache contention in the hot paths.
pico_set_binary_type(mtm_sitar copy_to_ram)
pico_set_linker_script(mtm_sitar ${CMAKE_CURRENT_LIST_DIR}/firmware/memmap_saved.ld)
pico_enable_stdio_usb(mtm_sitar 0)
pico_enable_stdio_uart(mtm_sitar 0)
target_link_options(mtm_sitar PRIVATE -Wl,--print-memory-usage)
pico_add_extra_outputs(mtm_sitar)

option(MTM_USB_EDITOR "Experimental direct USB CDC live editor" OFF)
if(MTM_USB_EDITOR)
    target_sources(mtm_sitar PRIVATE firmware/usb_editor.cpp firmware/usb_descriptors.cpp)
    target_include_directories(mtm_sitar PRIVATE firmware)
    target_compile_definitions(mtm_sitar PRIVATE MTM_USB_EDITOR=1)
    target_link_libraries(mtm_sitar tinyusb_device)
endif()
