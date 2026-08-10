cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pantograph C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pantograph pantograph.cpp)

# Catch accidental double-precision math (slow, no FPU); warn broadly.
target_compile_options(pantograph PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
# Print FLASH/RAM usage on every link — watch the gesture buffer grow.
target_link_options(pantograph PRIVATE -Wl,--print-memory-usage)
# Some boards need a longer oscillator startup or they won't run.
target_compile_definitions(pantograph PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_include_directories(pantograph PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(pantograph
  pico_stdlib
  pico_unique_id
  hardware_dma
  hardware_i2c
  hardware_pwm
  hardware_adc
  hardware_spi
)

pico_add_extra_outputs(pantograph)   # produces pantograph.uf2
pico_enable_stdio_usb(pantograph 0)  # USB is the audio DAC path; keep stdio off it
