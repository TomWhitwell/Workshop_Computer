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
set(CARD_NAME sheep)
# type matching project name here
project(sheep C CXX ASM)
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

# Create LoFi variant (8-bit audio, longer buffer)
add_card(${CARD_NAME}_lofi)
target_compile_definitions(${CARD_NAME}_lofi PRIVATE LOFI_MODE=1)

# Create HiFi variant (12-bit audio, shorter buffer) 
add_card(${CARD_NAME}_hifi)

# Single source of truth for the firmware version (keep in sync with info.yaml).
set(SHEEP_VERSION "1.2")

# Embed program metadata into the built artifacts (readable via `picotool info`).
pico_set_program_name(${CARD_NAME}_lofi "Sheep (LoFi)")
pico_set_program_version(${CARD_NAME}_lofi "${SHEEP_VERSION}")
pico_set_program_name(${CARD_NAME}_hifi "Sheep (HiFi)")
pico_set_program_version(${CARD_NAME}_hifi "${SHEEP_VERSION}")

# Rename each variant's default output to a version-stamped UF2
# (e.g. sheep_lofi.1.2.uf2 / sheep_hifi.1.2.uf2) and copy into the UF2/ directory.
add_custom_command(TARGET ${CARD_NAME}_lofi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename
        "$<TARGET_FILE_DIR:${CARD_NAME}_lofi>/${CARD_NAME}_lofi.uf2"
        "$<TARGET_FILE_DIR:${CARD_NAME}_lofi>/${CARD_NAME}_lofi.${SHEEP_VERSION}.uf2"
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE_DIR:${CARD_NAME}_lofi>/${CARD_NAME}_lofi.${SHEEP_VERSION}.uf2"
        ${CMAKE_CURRENT_SOURCE_DIR}/UF2/
    COMMENT "Creating version-stamped UF2: ${CARD_NAME}_lofi.${SHEEP_VERSION}.uf2"
    VERBATIM)

add_custom_command(TARGET ${CARD_NAME}_hifi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename
        "$<TARGET_FILE_DIR:${CARD_NAME}_hifi>/${CARD_NAME}_hifi.uf2"
        "$<TARGET_FILE_DIR:${CARD_NAME}_hifi>/${CARD_NAME}_hifi.${SHEEP_VERSION}.uf2"
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE_DIR:${CARD_NAME}_hifi>/${CARD_NAME}_hifi.${SHEEP_VERSION}.uf2"
        ${CMAKE_CURRENT_SOURCE_DIR}/UF2/
    COMMENT "Creating version-stamped UF2: ${CARD_NAME}_hifi.${SHEEP_VERSION}.uf2"
    VERBATIM)
