cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
project(simple_midi_2 C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

option(OPTIMIZE_SIZE "Optimize for size (-Os) instead of default -O2" OFF)

macro (add_example _name)
  add_executable(${ARGV})
  if (TARGET ${_name})

	# Add warnings, in particular about float/double conversion, as it's very easy to end up
	# with slower double-precision calculations when we only mean to use single-precision float.
	target_compile_options(${_name} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra -mtune=cortex-m0plus)
	if (OPTIMIZE_SIZE)
	  target_compile_options(${_name} PRIVATE -Os)
	endif()
	target_link_options(${_name} PRIVATE -Wl,--print-memory-usage)

	# Give oscillator more time to start - some boards won't run if this isn't included
	target_compile_definitions(${_name} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

	target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_link_libraries(${_name} pico_unique_id pico_stdlib  hardware_dma hardware_interp hardware_i2c hardware_pwm hardware_adc hardware_spi)
	pico_add_extra_outputs(${_name})
	target_sources(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/main.cpp)

	
	pico_set_binary_type(${_name} copy_to_ram)
	
	pico_enable_stdio_usb(${_name} 0)
  endif()
endmacro()



add_example(simple_midi_2)

target_link_libraries(simple_midi_2 pico_multicore  hardware_sync tinyusb_device tinyusb_host tinyusb_board)
target_sources(simple_midi_2 PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
	${CMAKE_CURRENT_LIST_DIR}/usb_midi_host.c
	${CMAKE_CURRENT_LIST_DIR}/usb_midi_host_app_driver.c)
