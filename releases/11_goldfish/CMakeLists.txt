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


add_executable(goldfish)
target_include_directories(goldfish PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(goldfish pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_flash pico_multicore)
pico_add_extra_outputs(goldfish)
target_sources(goldfish PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/goldfish_stream.c)
target_compile_definitions(goldfish PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(goldfish 0)

# Run the whole image from RAM: goldfish writes flash during operation, so no
# code (incl. SDK divider/float routines) may be fetched from flash while core 1
# is mid-erase. Matches MLRws.
pico_set_binary_type(goldfish copy_to_ram)

# Single source of truth for the firmware version (keep in sync with info.yaml).
set(GOLDFISH_VERSION "2.0")

# Embed program metadata into the built artifact (readable via `picotool info`).
pico_set_program_name(goldfish "Goldfish")
pico_set_program_version(goldfish "${GOLDFISH_VERSION}")

# Rename the default output to a version-stamped UF2 (e.g. goldfish.1.1.uf2) and
# copy that single, versioned artifact into the UF2/ directory.
add_custom_command(TARGET goldfish POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename
        "$<TARGET_FILE_DIR:goldfish>/goldfish.uf2"
        "$<TARGET_FILE_DIR:goldfish>/goldfish.${GOLDFISH_VERSION}.uf2"
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE_DIR:goldfish>/goldfish.${GOLDFISH_VERSION}.uf2"
        ${CMAKE_CURRENT_SOURCE_DIR}/UF2/
    COMMENT "Creating version-stamped UF2: goldfish.${GOLDFISH_VERSION}.uf2"
    VERBATIM)
