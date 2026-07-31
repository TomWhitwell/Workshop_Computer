cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(F1shnet C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(F1SHNET_VERSION "0.1.0")

pico_sdk_init()

add_executable(F1shnet
    main.cpp
)

target_include_directories(F1shnet PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(F1shnet PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_compile_options(F1shnet PRIVATE
    -O3
    -Wall
    -Wextra
    -Wdouble-promotion
    -Wfloat-conversion
)

target_link_libraries(F1shnet
    pico_unique_id
    pico_stdlib
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    hardware_clocks
)

pico_enable_stdio_usb(F1shnet 0)
pico_set_binary_type(F1shnet copy_to_ram)
pico_add_extra_outputs(F1shnet)

pico_set_program_name(F1shnet "F1shnet")
pico_set_program_version(F1shnet "${F1SHNET_VERSION}")

add_custom_command(TARGET F1shnet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename
        "$<TARGET_FILE_DIR:F1shnet>/F1shnet.uf2"
        "$<TARGET_FILE_DIR:F1shnet>/F1shnet.${F1SHNET_VERSION}.uf2"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE_DIR:F1shnet>/F1shnet.${F1SHNET_VERSION}.uf2"
        ${CMAKE_CURRENT_SOURCE_DIR}/UF2/
    COMMENT "Creating version-stamped UF2: F1shnet.${F1SHNET_VERSION}.uf2"
    VERBATIM)
