cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
project(computercard C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

macro (add_example _name)
  add_executable(${ARGV})
  if (TARGET ${_name})

	# Add warnings, in particular about float/double conversion, as it's very easy to end up
	# with slower double-precision calculations when we only mean to use single-precision float.
	target_compile_options(${_name} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
	target_link_options(${_name} PRIVATE -Wl,--print-memory-usage)
	
	# Give oscillator more time to start - some boards won't run if this isn't included
	target_compile_definitions(${_name} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
	
	target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
	target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/${_name}/)
    target_link_libraries(${_name} pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi)
	pico_add_extra_outputs(${_name})
	target_sources(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/${_name}/main.cpp)	  
	pico_enable_stdio_usb(${_name} 0)
  endif()
endmacro()
  

add_example(calibrated_cv_out)

add_example(midi_device)
target_link_libraries(midi_device pico_multicore tinyusb_device tinyusb_board )
target_sources(midi_device PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/midi_device/usb_descriptors.c)

add_example(midi_device_host)
target_link_libraries(midi_device_host pico_multicore tinyusb_host tinyusb_board  tinyusb_device  )
target_sources(midi_device_host PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/examples/midi_device_host/usb_midi_host.c
  ${CMAKE_CURRENT_LIST_DIR}/examples/midi_device_host/usb_midi_host_app_driver.c
  ${CMAKE_CURRENT_LIST_DIR}/examples/midi_device_host/usb_descriptors.c
)

add_example(midi_host)
target_sources(midi_host PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/midi_host/usb_midi_host.c  ${CMAKE_CURRENT_LIST_DIR}/examples/midi_host/usb_midi_host_app_driver.c)
target_link_libraries(midi_host pico_multicore tinyusb_host tinyusb_board  )

add_example(normalisation_probe)
  
add_example(passthrough)

add_example(sample_and_hold)

add_example(sample_upload)

add_example(second_core)
target_link_libraries(second_core pico_multicore)

add_example(sine_wave_lookup)

add_example(sine_wave_float)

add_example(usb_detect)

add_example(usb_serial)
target_link_libraries(usb_serial pico_multicore)
pico_enable_stdio_usb(usb_serial 1)
