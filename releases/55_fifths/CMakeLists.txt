# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
# Define a variable for the card name
set(CARD_NAME fifths)
# type matching project name here
project(fifths C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

macro (add_card _name)
    add_executable(${ARGV})
    if (TARGET ${_name})
      target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
      target_link_libraries(${_name} pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi)
      pico_add_extra_outputs(${_name})
      target_sources(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/main.cpp)
      target_compile_definitions(${_name} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
      pico_enable_stdio_usb(${_name} 1)
    endif()
  endmacro()
  
add_card(${CARD_NAME})

# Single source of truth for the firmware version (keep in sync with info.yaml).
set(FIFTHS_VERSION "1.1")

# Embed program metadata into the built artifact (readable via `picotool info`).
pico_set_program_name(${CARD_NAME} "Fifths")
pico_set_program_version(${CARD_NAME} "${FIFTHS_VERSION}")

# Rename the default output to a version-stamped UF2 (e.g. fifths.1.1.uf2) and
# copy that single, versioned artifact into the UF2/ directory.
add_custom_command(TARGET ${CARD_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename
        "$<TARGET_FILE_DIR:${CARD_NAME}>/${CARD_NAME}.uf2"
        "$<TARGET_FILE_DIR:${CARD_NAME}>/${CARD_NAME}.${FIFTHS_VERSION}.uf2"
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE_DIR:${CARD_NAME}>/${CARD_NAME}.${FIFTHS_VERSION}.uf2"
        ${CMAKE_CURRENT_SOURCE_DIR}/UF2/
    COMMENT "Creating version-stamped UF2: ${CARD_NAME}.${FIFTHS_VERSION}.uf2"
    VERBATIM)