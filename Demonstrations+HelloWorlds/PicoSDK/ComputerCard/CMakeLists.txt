set(PICO_BOARD wscomputer CACHE STRING "Board type")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR})

cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
project(computercard C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

macro (add_example _name)
    add_executable(${ARGV})
    if (TARGET ${_name})
	  target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
	  target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/${_name}/)
      target_link_libraries(${_name} pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi)
	  pico_add_extra_outputs(${_name})
	  target_sources(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/${_name}/main.cpp)
	  
	  pico_enable_stdio_usb(${_name} 1)
    endif()
  endmacro()
  

add_example(normalisation_probe)
add_example(passthrough)
add_example(sample_and_hold)
add_example(talkie_pcm)

add_example(midi_device)
target_link_libraries(midi_device pico_multicore tinyusb_device tinyusb_board )
target_sources(midi_device PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/midi_device/usb_descriptors.c)
pico_enable_stdio_usb(midi_device 0)
