cmake_minimum_required (VERSION 3.13)

# Establish pico board type
set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(clockwork C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(clockwork)

pico_set_binary_type(clockwork copy_to_ram)

target_sources(clockwork PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_midi_host.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_midi_host_app_driver.c
)

target_include_directories(clockwork PUBLIC 
    ${CMAKE_CURRENT_LIST_DIR} 
    ${CMAKE_CURRENT_LIST_DIR}/ComputerCard
)

# Avoid startup timing problems
target_compile_definitions(clockwork PUBLIC PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
target_compile_definitions(clockwork PRIVATE PICO_DEFAULT_UART_BAUD_RATE=115200)
target_compile_definitions(clockwork PUBLIC 
    PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
)

pico_enable_stdio_uart(clockwork 0)
pico_enable_stdio_usb(clockwork 0)
pico_add_extra_outputs(clockwork)

target_compile_options(clockwork PRIVATE 
    -Wdouble-promotion 
    -Wfloat-conversion 
    -Wall 
    -Wextra 
    -O3 
    -ffast-math
    -fno-jump-tables
)
target_link_options(clockwork PRIVATE -Wl,--print-memory-usage)
	
target_link_libraries(clockwork 
    pico_unique_id 
    pico_stdlib 
    hardware_dma 
    hardware_i2c 
    hardware_pwm 
    hardware_adc 
    hardware_spi 
    hardware_timer 
    pico_multicore 
    hardware_flash 
    hardware_vreg 
    hardware_clocks
    tinyusb_device
    tinyusb_host
    tinyusb_board
)
