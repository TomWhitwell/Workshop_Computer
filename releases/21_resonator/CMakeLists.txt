cmake_minimum_required(VERSION 3.13)

# Set board type for Raspberry Pi Pico
set(PICO_BOARD pico)

# Include Pico SDK
include(pico_sdk_import.cmake)

# Project name and languages
project(resonator C CXX ASM)

# Set C++ standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Add executable target
add_executable(resonator
    main.cpp
    serial_flash.cpp
)

# Link libraries required by the Pico SDK and ComputerCard
target_link_libraries(resonator
    pico_stdlib
    hardware_gpio
    hardware_pwm
    hardware_adc
    hardware_dma
    hardware_spi
    hardware_i2c
    hardware_flash
    hardware_irq
    hardware_clocks
    pico_multicore
    pico_unique_id
)

# Enlarge the USB CDC FIFOs. The SDK defaults are 64 bytes each, and the web UI's connect
# sequence asks seven questions at once whose answers total ~77 bytes. Overflowing the TX
# FIFO makes stdio_usb_out_chars spin calling tud_task() from Core 1 (for up to
# PICO_STDIO_USB_STDOUT_TIMEOUT_US = 500ms) while USBCTRL_IRQ runs TinyUSB on Core 0.
# TinyUSB is not safe to drive from two cores, and that wedges the CDC endpoint
# permanently: control transfers keep working but bulk data never flows again.
# The SDK's tusb_config.h guards both with #ifndef, so defining them here wins.
target_compile_definitions(resonator PRIVATE
    CFG_TUD_CDC_TX_BUFSIZE=256
    CFG_TUD_CDC_RX_BUFSIZE=256
    # Disable the SDK's background task. It calls tud_task() from Core 0's alarm, which
    # would race the USB device driver now that USBCTRL_IRQ has been moved to Core 1.
    # Core 1 pumps tud_task() itself instead (see core1_handler).
    PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=0
)

# Run from RAM to prevent flash access delays during audio
pico_set_binary_type(resonator copy_to_ram)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(resonator)

target_compile_definitions(resonator PUBLIC PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

# Enable USB stdio
pico_enable_stdio_usb(resonator 1)
pico_enable_stdio_uart(resonator 0)

# Set compiler flags for optimization
target_compile_options(resonator PRIVATE
    -Wall
    -Wextra
    -O2
    -ffast-math
    -funroll-loops
)

# Define preprocessor macros
target_compile_definitions(resonator PRIVATE
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
)

# Include directories
target_include_directories(resonator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
