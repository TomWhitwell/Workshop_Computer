cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(bird_card VERSION 0.5.0 LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(bird_card
	main.cpp
)

target_compile_options(bird_card PRIVATE
	-Wdouble-promotion
	-Wfloat-conversion
	-Wall
	-Wextra
)

target_compile_definitions(bird_card PRIVATE
	PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
	BIRDS_CARD_NAME="Birds"
	BIRDS_CARD_NUMBER=44
	BIRDS_CARD_VERSION="0.5.0"
)

target_include_directories(bird_card PUBLIC
	${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(bird_card
	pico_unique_id
	pico_stdlib
	hardware_dma
	hardware_i2c
	hardware_pwm
	hardware_adc
	hardware_spi
)

pico_set_binary_type(bird_card copy_to_ram)
pico_set_program_name(bird_card "Birds")
pico_set_program_version(bird_card "0.5.0")
pico_enable_stdio_usb(bird_card 0)
pico_add_extra_outputs(bird_card)
