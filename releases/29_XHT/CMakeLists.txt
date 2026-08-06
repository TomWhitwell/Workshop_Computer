cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "$ENV{HOME}/pico-sdk" CACHE PATH "Path to pico-sdk")
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(XHTCard C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

find_program(PICOTOOL_EXE
    NAMES picotool
    PATHS
        ${CMAKE_BINARY_DIR}/_deps/picotool
        $ENV{HOME}/coding/GitHub/CozmikC1zzl3/build/_deps/picotool
        NO_DEFAULT_PATH)

add_executable(XHTCard XHTCard.cpp)
add_executable(XHTCardMidi
    XHTCardMidi.cpp
    usb_descriptors.c
    usb_midi_host.c
    usb_midi_host_app_driver.c)

# Use the authoritative, current ComputerCard header rather than a release copy.
set(COMPUTERCARD_DIR "$ENV{HOME}/coding/GitHub/Workshop_Computer/Demonstrations+HelloWorlds/PicoSDK/ComputerCard"
    CACHE PATH "Directory containing the latest ComputerCard.h")
target_include_directories(XHTCard PRIVATE ${COMPUTERCARD_DIR})
target_include_directories(XHTCardMidi PRIVATE ${COMPUTERCARD_DIR} .)

target_compile_options(XHTCard PRIVATE -O3 -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_compile_options(XHTCardMidi PRIVATE -O3 -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_compile_definitions(XHTCard PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
target_compile_definitions(XHTCardMidi PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
target_link_options(XHTCard PRIVATE -Wl,--print-memory-usage)
target_link_options(XHTCardMidi PRIVATE -Wl,--print-memory-usage)
target_link_libraries(XHTCard pico_stdlib pico_unique_id hardware_adc hardware_clocks
    hardware_dma hardware_i2c hardware_interp hardware_pwm hardware_spi)
target_link_libraries(XHTCardMidi pico_stdlib pico_unique_id pico_multicore
    hardware_adc hardware_clocks hardware_dma hardware_i2c hardware_interp hardware_pwm hardware_spi
    tinyusb_device tinyusb_host tinyusb_board)

function(export_top_level_uf2 target_name output_name)
    if(PICOTOOL_EXE)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${PICOTOOL_EXE} uf2 convert
                ${CMAKE_CURRENT_BINARY_DIR}/${target_name}.elf -t elf
                ${CMAKE_CURRENT_SOURCE_DIR}/${output_name} -t uf2
            BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/${output_name}
            VERBATIM)
    else()
        message(WARNING "picotool not found: top-level UF2 export for ${target_name} will not run automatically")
    endif()
endfunction()

pico_enable_stdio_usb(XHTCard 0)
pico_enable_stdio_uart(XHTCard 0)
pico_set_binary_type(XHTCard copy_to_ram)
pico_add_extra_outputs(XHTCard)
export_top_level_uf2(XHTCard uf2/XHTCard.uf2)
pico_enable_stdio_usb(XHTCardMidi 0)
pico_enable_stdio_uart(XHTCardMidi 0)
pico_set_binary_type(XHTCardMidi copy_to_ram)
pico_add_extra_outputs(XHTCardMidi)
export_top_level_uf2(XHTCardMidi uf2/XHTCardMidi.uf2)
