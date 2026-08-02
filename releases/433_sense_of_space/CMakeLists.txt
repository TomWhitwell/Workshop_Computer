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
set(PICO_FLASH_SIZE_BYTES "16777216" CACHE STRING "Flash size large enough for both generated variants")
cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(433_sense_of_space C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

function(add_433_sense_of_space_variant target sample_rate)
    add_executable(${target}
        main.cpp
        reverb_dsp.c
        sample.S
    )

    target_compile_options(${target} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
    target_link_options(${target} PRIVATE -Wl,--print-memory-usage)
    target_compile_definitions(${target} PRIVATE
        PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
        FOUR33_SAMPLE_RATE=${sample_rate}
    )
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_link_libraries(${target}
        pico_unique_id
        pico_stdlib
        hardware_dma
        hardware_i2c
        hardware_pwm
        hardware_adc
        hardware_spi
        hardware_clocks
    )

    pico_add_extra_outputs(${target})
    pico_enable_stdio_usb(${target} 0)
    pico_enable_stdio_uart(${target} 0)
endfunction()

add_433_sense_of_space_variant(433_sense_of_space_2mb 10000)
add_433_sense_of_space_variant(433_sense_of_space_16mb 24000)
