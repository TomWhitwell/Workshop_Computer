cmake_minimum_required(VERSION 3.13)

# Keep the full redistribution notices beside generated firmware outputs.
configure_file("${CMAKE_CURRENT_LIST_DIR}/NOTICE.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/NOTICE.txt" COPYONLY)

include(pico_sdk_import.cmake)

project(VortexRunner C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Both editions share the synth, Web MIDI protocol and saved-preset formats.
# The standard target omits EightMU discovery, layer controls and LED takeover.
function(add_vortex_firmware target enable_8mu output_name)
    add_executable(${target}
        CS80.cpp
        CS80_LUT.cpp
        usb_descriptors.c
        usb_midi_host.c
        usb_midi_host_app_driver.c
    )

    target_include_directories(${target} PRIVATE
        .
    )

    target_link_libraries(${target}
        pico_stdlib
        pico_unique_id
        pico_multicore
        tinyusb_device
        tinyusb_host
        tinyusb_board
        hardware_adc
        hardware_pwm
        hardware_dma
        hardware_spi
        hardware_clocks
        hardware_i2c
    )

    target_compile_definitions(${target} PRIVATE
        PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
        CS80_OVERCLOCK_KHZ=192000
        VORTEX_ENABLE_8MU=${enable_8mu}
        PICO_STACK_SIZE=0x1000
    )

    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output_name})
    pico_enable_stdio_usb(${target} 0)
    pico_set_binary_type(${target} copy_to_ram)

    target_compile_options(${target} PRIVATE
        -O3
        -Wdouble-promotion
        -Wfloat-conversion
        -Wall
        -Wextra
    )

    target_link_options(${target} PRIVATE
        -Wl,--print-memory-usage
    )

    pico_add_extra_outputs(${target})
endfunction()

add_vortex_firmware(VortexRunner 0 Vortex_Runner_0.1.2)
add_vortex_firmware(VortexRunner8mu 1 Vortex_Runner_8mu_0.1.2)
