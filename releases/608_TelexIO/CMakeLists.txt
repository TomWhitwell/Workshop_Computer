cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
project(telexio C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(telexio
	main.cpp
	i2c_multi.c
	lut.cpp
    	TELEXi/AnalogReader.cpp
    	TELEXo/Quantizer.cpp
   	TELEXo/TxHelper.cpp
   	TELEXo/Oscillator.cpp
  	TELEXo/CVOutput.cpp
   	TELEXo/TriggerOutput.cpp
    	TELEXo/Output.cpp
    	telexio.cpp
)

target_link_options(telexio PRIVATE
     -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/telexio.map
     -Wl,--print-memory-usage
)

pico_set_binary_type(telexio copy_to_ram)

target_include_directories(telexio PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/TELEXi
    ${CMAKE_CURRENT_LIST_DIR}/TELEXo
)

target_compile_definitions(telexio PRIVATE
  ENABLE_UART_DEBUGGING
  PICO_FLOAT_IN_RAM=1
  PICO_DOUBLE_IN_RAM=1
  PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

pico_generate_pio_header(telexio
	${CMAKE_CURRENT_LIST_DIR}/i2c_multi.pio
)

target_compile_options(telexio PRIVATE
    -O2
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
    -Winline
)

pico_add_extra_outputs(telexio)

target_link_libraries(telexio
    pico_stdlib
    hardware_irq
    hardware_pio
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_dma
    hardware_spi
    pico_multicore
    pico_rand
)

pico_enable_stdio_usb(telexio 1)
pico_enable_stdio_uart(telexio 0)
