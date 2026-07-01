cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
project(computercard C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()


macro (add_program _name)
    add_executable(${ARGV})
    if (TARGET ${_name})
	  target_compile_definitions(${_name} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
	  target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
      target_link_libraries(${_name} pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi)
	  pico_add_extra_outputs(${_name})
	  target_sources(${_name} PUBLIC ${_name}.cpp)
	  target_compile_definitions(${_name} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

	  pico_enable_stdio_usb(${_name} 0)
    endif()
  endmacro()


add_program(origami)
