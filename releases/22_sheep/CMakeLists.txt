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

# Copy both UF2 files to UF2 directory with descriptive names
add_custom_command(
    TARGET ${CARD_NAME}_lofi
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/${CARD_NAME}_lofi.uf2
            ${CMAKE_CURRENT_SOURCE_DIR}/UF2/${CARD_NAME}_lofi.uf2
)

add_custom_command(
    TARGET ${CARD_NAME}_hifi
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/${CARD_NAME}_hifi.uf2
            ${CMAKE_CURRENT_SOURCE_DIR}/UF2/${CARD_NAME}_hifi.uf2
)
