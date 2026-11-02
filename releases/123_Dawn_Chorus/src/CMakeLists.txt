cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)
project(dawn_chorus_test C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(dawn_chorus_test main.cpp)

target_link_libraries(dawn_chorus_test
    pico_stdlib
    pico_multicore
    hardware_adc
    hardware_dma
    hardware_flash
    hardware_i2c
    hardware_irq
    hardware_pwm
    hardware_spi
)

# USB serial for the host companion; UART off
pico_enable_stdio_usb(dawn_chorus_test 1)
pico_enable_stdio_uart(dawn_chorus_test 0)

# Run everything from RAM: no flash stalls in the audio interrupt
pico_set_binary_type(dawn_chorus_test copy_to_ram)

# Run at 200 MHz (RP2040's fastest officially supported clock; the SDK raises the core
# voltage to suit). ComputerCard's sample rate comes from the separate 48 MHz USB clock,
# so audio timing is unchanged; this just gives the audio interrupt more headroom.
target_compile_definitions(dawn_chorus_test PRIVATE PICO_USE_FASTEST_SUPPORTED_CLOCK=1)

pico_add_extra_outputs(dawn_chorus_test)

# Test build at the RP2040's standard 125 MHz, otherwise identical
add_executable(dawn_chorus_test_125mhz main.cpp)
target_link_libraries(dawn_chorus_test_125mhz
    pico_stdlib pico_multicore hardware_adc hardware_dma hardware_flash
    hardware_i2c hardware_irq hardware_pwm hardware_spi)
pico_enable_stdio_usb(dawn_chorus_test_125mhz 1)
pico_enable_stdio_uart(dawn_chorus_test_125mhz 0)
pico_set_binary_type(dawn_chorus_test_125mhz copy_to_ram)
pico_add_extra_outputs(dawn_chorus_test_125mhz)

