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
project(byo_benjolin C CXX ASM)
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
  
add_card(byo_benjolin)

# Single source of truth for the firmware version (keep in sync with info.yaml).
set(BYOB_VERSION "1.1")

# Embed program metadata into the built artifact (readable via `picotool info`).
pico_set_program_name(byo_benjolin "BYO Benjolin")
pico_set_program_version(byo_benjolin "${BYOB_VERSION}")

# Emit a single, version-stamped UF2 (e.g. byo_benjolin.1.1.uf2) by renaming the
# default output, so we don't also ship an unversioned artifact.
add_custom_command(TARGET byo_benjolin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename
        "$<TARGET_FILE_DIR:byo_benjolin>/byo_benjolin.uf2"
        "$<TARGET_FILE_DIR:byo_benjolin>/byo_benjolin.${BYOB_VERSION}.uf2"
    COMMENT "Creating version-stamped UF2: byo_benjolin.${BYOB_VERSION}.uf2"
    VERBATIM)