# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")


cmake_minimum_required(VERSION 3.13)

# Pull in the Pico SDK
include(pico_sdk_import.cmake)


# Define the project name
project(TuringMatrix C CXX ASM)

pico_sdk_init()

set(PICO_BOOT_STAGE2 boot2_w25q080.S)


# Create the executable from your source files
add_executable(${PROJECT_NAME}
    main.cpp
    MainApp.cpp
    Clock.cpp
    Config.cpp
    UI.cpp
    Turing.cpp
)
pico_set_binary_type(${PROJECT_NAME} copy_to_ram) # This line in this spot actually works! 

# Add include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}        # local .h files
)

# Give oscillator more time to start - workshop system won't start if this isn't included 
target_compile_definitions(${PROJECT_NAME} PRIVATE 
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64

    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=1000
    CFG_TUSB_MCU=OPT_MCU_RP2040
    CFG_TUSB_RHPORT0_MODE=OPT_MODE_DEVICE
    CFG_TUD_ENABLED=1
    CFG_TUD_MIDI=1  # <-- Enable MIDI device

)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf") 


target_sources(${PROJECT_NAME} PRIVATE
    usb_descriptors.c
)


# Link against the libraries you’re using (as per ComputerCard dependencies)
target_link_libraries(${PROJECT_NAME}
pico_unique_id 
pico_stdlib 
hardware_dma 
hardware_i2c 
hardware_pwm 
hardware_adc 
hardware_spi 
hardware_flash 
hardware_sync 
pico_stdio_usb 
pico_platform 
pico_multicore  

tinyusb_device
tinyusb_board


)

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_semihosting(${PROJECT_NAME} 0)  # Enable semihosting
 

# Generate UF2, bin, and other output formats
pico_add_extra_outputs(${PROJECT_NAME})
