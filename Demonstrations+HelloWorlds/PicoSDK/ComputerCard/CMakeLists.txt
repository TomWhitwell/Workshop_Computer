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

# EightMU.h carries both the usb_midi_host driver and the host-mode TinyUSB
# configuration, so an 8mu card needs no extra sources - just the TinyUSB host
# libraries, and CFG_TUSB_CONFIG_FILE to point TinyUSB at EightMU.h for its
# configuration.
add_example(eightmu)
target_compile_definitions(eightmu PRIVATE CFG_TUSB_CONFIG_FILE="EightMU.h")
target_link_libraries(eightmu pico_multicore tinyusb_host tinyusb_board)

add_example(calibrated_input)

add_example(card_info)

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

add_example(hid_keyboard_mouse)
target_link_libraries(hid_keyboard_mouse pico_multicore tinyusb_host tinyusb_board)

add_example(midi_host)
target_sources(midi_host PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/midi_host/usb_midi_host.c  ${CMAKE_CURRENT_LIST_DIR}/examples/midi_host/usb_midi_host_app_driver.c)
target_link_libraries(midi_host pico_multicore tinyusb_host tinyusb_board  )

add_example(normalisation_probe)
  
add_example(passthrough)

add_example(sample_and_hold)

add_example(web_interface)
target_link_libraries(web_interface pico_multicore tinyusb_device tinyusb_board )
target_sources(web_interface PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/web_interface/usb_descriptors.c)

add_example(sample_upload)

add_example(second_core)
target_link_libraries(second_core pico_multicore)

add_example(sine_wave_lookup)

add_example(sine_wave_float)

add_example(usb_detect)

add_example(usb_serial)
target_link_libraries(usb_serial pico_multicore)
pico_enable_stdio_usb(usb_serial 1)

# usb_msc_host: play and record WAV files on a USB stick (USB host, needs Rev1_1
# hardware). It needs TinyUSB 0.21 or later, which has double-buffered RP2040
# host transfers (~1MB/s). Older TinyUSB, such as 0.18 bundled with SDK 2.1,
# manages only ~64kB/s of USB host mass storage reads, too slow for audio.
# So this example is skipped (with a warning) if the TinyUSB in use is older.
# To use a newer TinyUSB than the SDK's, configure with
#   cmake -DPICO_TINYUSB_PATH=/path/to/tinyusb ..
# The check reads the version from TinyUSB itself, so it will pass without any
# extra options once the SDK bundles TinyUSB 0.21 or later.
if (PICO_TINYUSB_PATH)
  set(_tinyusb_dir ${PICO_TINYUSB_PATH})
else()
  set(_tinyusb_dir ${PICO_SDK_PATH}/lib/tinyusb)
endif()
set(_tinyusb_major 0)
set(_tinyusb_minor 0)
if (EXISTS ${_tinyusb_dir}/src/tusb_option.h)
  file(STRINGS ${_tinyusb_dir}/src/tusb_option.h _tinyusb_ver REGEX "^#define TUSB_VERSION_(MAJOR|MINOR) ")
  foreach(_line ${_tinyusb_ver})
    if (_line MATCHES "TUSB_VERSION_MAJOR +([0-9]+)")
      set(_tinyusb_major ${CMAKE_MATCH_1})
    elseif (_line MATCHES "TUSB_VERSION_MINOR +([0-9]+)")
      set(_tinyusb_minor ${CMAKE_MATCH_1})
    endif()
  endforeach()
endif()

if (_tinyusb_major GREATER 0 OR _tinyusb_minor GREATER_EQUAL 21)
  # Runs from RAM, as core 0's USB/FatFs activity would otherwise evict core 1's
  # audio code from the shared XIP cache and upset ComputerCard's ADC timing.
  add_example(usb_msc_host)
  target_sources(usb_msc_host PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/examples/usb_msc_host/diskio.c
    ${CMAKE_CURRENT_LIST_DIR}/examples/usb_msc_host/fatfs/ff.c
    ${CMAKE_CURRENT_LIST_DIR}/examples/usb_msc_host/fatfs/ffunicode.c)
  target_include_directories(usb_msc_host PRIVATE ${CMAKE_CURRENT_LIST_DIR}/examples/usb_msc_host/fatfs)
  target_link_libraries(usb_msc_host pico_multicore tinyusb_host tinyusb_board)
  pico_enable_stdio_uart(usb_msc_host 0)
  pico_set_binary_type(usb_msc_host copy_to_ram)
else()
  if (EXISTS ${_tinyusb_dir}/src/tusb_option.h)
    set(_tinyusb_found "found ${_tinyusb_major}.${_tinyusb_minor}")
  else()
    set(_tinyusb_found "found no TinyUSB (src/tusb_option.h is missing)")
  endif()
  message(WARNING "usb_msc_host SKIPPED: it needs TinyUSB 0.21 or later, but ${_tinyusb_found} in ${_tinyusb_dir}. Configure with -DPICO_TINYUSB_PATH=/path/to/tinyusb to build it (use an absolute path or $HOME; a ~ is not expanded inside -D options).")
endif()
