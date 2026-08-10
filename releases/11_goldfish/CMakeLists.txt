# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
project(goldfish C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# Diagnostics/instrumentation (timing counters read via GDB) are compiled out of
# release builds. Configure with -DGOLDFISH_DEBUG=ON to build an instrumented image.
option(GOLDFISH_DEBUG "Enable Goldfish diagnostics/instrumentation" OFF)

# Single source of truth for the firmware version (keep in sync with info.yaml).
set(GOLDFISH_VERSION "2.0")

function(add_goldfish_variant target suffix flash_bytes)
    add_executable(${target})
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_link_libraries(${target} pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_flash pico_multicore)
    target_sources(${target} PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/goldfish_stream.c)
    target_compile_definitions(${target} PRIVATE
        PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
        GOLDFISH_FLASH_SIZE_BYTES=${flash_bytes})
    if(GOLDFISH_DEBUG)
        target_compile_definitions(${target} PRIVATE GOLDFISH_DEBUG=1)
    endif()

    pico_enable_stdio_usb(${target} 0)

    # Run the whole image from RAM: goldfish writes flash during operation, so no
    # code (incl. SDK divider/float routines) may be fetched from flash while core 1
    # is mid-erase. Matches MLRws.
    pico_set_binary_type(${target} copy_to_ram)

    # Embed program metadata into the built artifact (readable via `picotool info`).
    pico_set_program_name(${target} "Goldfish")
    pico_set_program_version(${target} "${GOLDFISH_VERSION}.${suffix}")

    pico_add_extra_outputs(${target})

    # Rename the default output to a version- and card-size-stamped UF2 and copy it
    # into the release UF2/ directory.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rename
            "$<TARGET_FILE_DIR:${target}>/${target}.uf2"
            "$<TARGET_FILE_DIR:${target}>/goldfish.${GOLDFISH_VERSION}.${suffix}.uf2"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE_DIR:${target}>/goldfish.${GOLDFISH_VERSION}.${suffix}.uf2"
            ${CMAKE_CURRENT_SOURCE_DIR}/UF2/
        COMMENT "Creating version-stamped UF2: goldfish.${GOLDFISH_VERSION}.${suffix}.uf2"
        VERBATIM)
endfunction()

add_goldfish_variant(goldfish_2mb 2mb "(2u*1024u*1024u)")
add_goldfish_variant(goldfish_16mb 16mb "(16u*1024u*1024u)")
