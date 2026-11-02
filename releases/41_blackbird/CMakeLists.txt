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

cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
# Debug harness build (make debug): debugger mailbox, input overrides and a
# mirror of USB output, for util/bbdbg.py. Same optimisation as release so
# timings are representative. Builds blackbird_debug and leaves UF2/ alone.
option(BB_DEBUG_HARNESS "Build with the SWD debug/test harness" OFF)
# 24kHz: measured on hardware, core 1 is ~68% busy at worst (stress test) at
# this rate; 40kHz+ saturates it and drops samples. Was 9.6kHz up to v1.2.
set(BB_SAMPLE_RATE_HZ 24000 CACHE STRING "Audio ISR / output rendering rate (must divide 6000000)")

# Define a variable for the card name
if(BB_DEBUG_HARNESS)
    set(CARD_NAME blackbird_debug)
else()
    set(CARD_NAME blackbird)
endif()
# type matching project name here
project(blackbird C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

macro (add_card _name)
    add_executable(${ARGV})
    if (TARGET ${_name})
            target_include_directories(${_name} PUBLIC 
                ${CMAKE_CURRENT_LIST_DIR}
            )
            target_link_libraries(${_name} pico_unique_id pico_stdlib pico_multicore hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_flash lua tinyusb_device tinyusb_host tinyusb_board)
      pico_add_extra_outputs(${_name})
      target_sources(${_name} PUBLIC 
        ${CMAKE_CURRENT_LIST_DIR}/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/casl.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/slopes.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/ashapes.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/detect.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/caw.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/ii.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/ii_bus.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/ii_codec.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/metro.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/clock.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/clock_ll.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/events_lockfree.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/usb_lockfree.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/l_bootstrap.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/l_crowlib.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/l_ii_mod.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/ll_timers.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/random.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/wrblocks.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/mailbox.c
                ${CMAKE_CURRENT_LIST_DIR}/lib/fastmath.c
                ${CMAKE_CURRENT_LIST_DIR}/lib/fastmath_lut.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/flash_storage.cpp
        ${CMAKE_CURRENT_LIST_DIR}/lib/lua_pool.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/usb_midi_host.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/usb_midi_host_app_driver.c
      )
      target_compile_definitions(${_name} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
      # Disable stdio USB - we're using TinyUSB directly with custom descriptors for druid compatibility
      pico_enable_stdio_usb(${_name} 0)
      pico_enable_stdio_uart(${_name} 0)
    endif()
  endmacro()

# Add Lua library
add_subdirectory(lua/src)

# Build host luac before compiling Lua files to bytecode
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/host_luac/luac
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/util/build_host_luac.sh
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/luac.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lapi.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lauxlib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lbaselib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lcode.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lcorolib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lctype.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ldblib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ldebug.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ldo.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ldump.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lfunc.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lgc.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/linit.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/liolib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/llex.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lmathlib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lmem.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/loadlib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lobject.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lopcodes.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/loslib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lparser.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lstate.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lstring.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lstrlib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ltable.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ltablib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ltm.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lundump.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lutf8lib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lvm.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lzio.c
            ${CMAKE_CURRENT_SOURCE_DIR}/util/build_host_luac.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building host luac for Lua bytecode compilation"
)

add_custom_target(build_host_luac DEPENDS ${CMAKE_BINARY_DIR}/host_luac/luac)

# Function to add Lua-to-header conversion
function(add_lua_header LUA_FILE HEADER_VAR)
    get_filename_component(LUA_NAME ${LUA_FILE} NAME_WE)
    set(HEADER_FILE ${CMAKE_BINARY_DIR}/${LUA_NAME}.h)
    
    add_custom_command(
        OUTPUT ${HEADER_FILE}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/util/lua2header.py 
                ${CMAKE_CURRENT_SOURCE_DIR}/${LUA_FILE} 
                ${HEADER_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${LUA_FILE} build_host_luac
        COMMENT "Converting ${LUA_FILE} to C header (bytecode or source fallback)"
    )
    
    set(${HEADER_VAR} ${HEADER_FILE} PARENT_SCOPE)
endfunction()

# Generate headers for core Lua libraries (always included)
add_lua_header(lib/lib-lua/asl.lua ASL_HEADER)
add_lua_header(lib/lib-lua/asllib.lua ASLLIB_HEADER)
add_lua_header(lib/lib-lua/crowlib.lua CROWLIB_HEADER)
add_lua_header(lib/lib-lua/clock.lua CLOCK_HEADER)
add_lua_header(lib/lib-lua/metro.lua METRO_HEADER)
add_lua_header(lib/lib-lua/public.lua PUBLIC_HEADER)
add_lua_header(lib/lib-lua/input.lua INPUT_HEADER)
add_lua_header(lib/lib-lua/output.lua OUTPUT_HEADER)
add_lua_header(lib/lib-lua/ii.lua II_HEADER)
add_lua_header(lib/lib-lua/calibrate.lua CALIBRATE_HEADER)
add_lua_header(lib/lib-lua/sequins.lua SEQUINS_HEADER)
add_lua_header(lib/lib-lua/quote.lua QUOTE_HEADER)
add_lua_header(lib/lib-lua/timeline.lua TIMELINE_HEADER)
add_lua_header(lib/lib-lua/hotswap.lua HOTSWAP_HEADER)
add_lua_header(lib/lib-lua/First.lua FIRST_HEADER)


# ii device tables, generated from crow's device descriptors (lib/ii/*.lua) by
# crow's generators, run with a host Lua interpreter built here
file(GLOB HOST_LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/*.c)
list(REMOVE_ITEM HOST_LUA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/luac.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/linit_crow.c)
set(HOST_LUA ${CMAKE_BINARY_DIR}/host_lua/lua)
add_custom_command(
    OUTPUT ${HOST_LUA}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/host_lua
    COMMAND gcc -O1 -DLUA_USE_C89 -I${CMAKE_CURRENT_SOURCE_DIR}/lua/src ${HOST_LUA_SOURCES} -lm -o ${HOST_LUA}
    DEPENDS ${HOST_LUA_SOURCES}
    COMMENT "Building host lua for the ii generators"
)
file(GLOB II_DESCRIPTORS ${CMAKE_CURRENT_SOURCE_DIR}/lib/ii/*.lua)
list(SORT II_DESCRIPTORS)
foreach(II_GEN ii_c_layer ii_mod_gen)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/${II_GEN}.h
        COMMAND ${HOST_LUA} ${CMAKE_CURRENT_SOURCE_DIR}/util/${II_GEN}.lua ${CMAKE_BINARY_DIR}/${II_GEN}.h ${II_DESCRIPTORS}
        DEPENDS ${HOST_LUA} ${CMAKE_CURRENT_SOURCE_DIR}/util/${II_GEN}.lua ${II_DESCRIPTORS}
        COMMENT "Generating ${II_GEN}.h from the ii descriptors"
    )
endforeach()

message(STATUS "Blackbird: Production build - no test scripts embedded")
set(TEST_DEFINE "PRODUCTION_BUILD")


# Create card
add_card(${CARD_NAME})

# Add generated headers as dependencies
target_sources(${CARD_NAME} PRIVATE 
  ${ASL_HEADER}
  ${ASLLIB_HEADER}
  ${CROWLIB_HEADER}
  ${CLOCK_HEADER}
  ${METRO_HEADER}
  ${PUBLIC_HEADER}
  ${INPUT_HEADER}
  ${OUTPUT_HEADER}
  ${II_HEADER}
  ${CMAKE_BINARY_DIR}/ii_c_layer.h
  ${CMAKE_BINARY_DIR}/ii_mod_gen.h
  ${CALIBRATE_HEADER}
  ${SEQUINS_HEADER}
  ${QUOTE_HEADER}
  ${TIMELINE_HEADER}
  ${HOTSWAP_HEADER}
  ${FIRST_HEADER}
)

# Add conditional compilation defines
target_compile_definitions(${CARD_NAME} PRIVATE ${TEST_DEFINE} PROCESS_SAMPLE_RATE_HZ_INT=${BB_SAMPLE_RATE_HZ})

# ii (I2C) on the UART pads, GPIO 0 (SDA) / 1 (SCL): I2C0 reads the EEPROM
# calibration at boot, then moves there (ComputerCard.h)
target_compile_definitions(${CARD_NAME} PRIVATE COMPUTERCARD_II_ON_UART_PINS)
target_include_directories(${CARD_NAME} PRIVATE ${CMAKE_BINARY_DIR})

# Performance optimizations for main code
target_compile_options(${CARD_NAME} PRIVATE
    -O3                    # Maximum speed optimization
    -ffast-math           # Faster floating-point (safe for audio DSP)
    -fsingle-precision-constant  # Treat float literals as float, not double (critical for RP2040 performance)
    # Note: -fno-exceptions and -fno-rtti already set by pico-sdk
    # Note: -flto disabled due to naming conflict with clock.h/clock()
    -fno-jump-tables      # switch tables go to .rodata (flash) even for RAM functions; core1 must not read flash
)

# Core 1 (audio) runs entirely from RAM so it keeps running while core 0 writes
# flash. Put the SDK's compiler runtime helpers it uses in RAM too.
# Check with: python3 util/core1_flash_audit.py build/blackbird.elf
target_compile_definitions(${CARD_NAME} PRIVATE
    PICO_FLOAT_IN_RAM=1
    PICO_DIVIDER_IN_RAM=1
    PICO_INT64_OPS_IN_RAM=1
    PICO_MEM_IN_RAM=1
    PICO_BITS_IN_RAM=1
    # Let malloc return NULL instead of panicking. Lua's allocator handles NULL
    # by running an emergency GC and retrying, then raising "not enough memory".
    PICO_MALLOC_PANIC=0
)

# All USB serial writes go through __wrap_tud_cdc_n_write (main.cpp), which
# never sends a partial message.
target_link_options(${CARD_NAME} PRIVATE -Wl,--wrap=tud_cdc_n_write)

if(BB_DEBUG_HARNESS)
    target_compile_definitions(${CARD_NAME} PRIVATE BB_DEBUG_HARNESS COMPUTERCARD_INPUT_OVERRIDES COMPUTERCARD_PROFILE)
else()
# The release UF2 is named with the version from info.yaml (blackbird.1.2.release.uf2),
# as the site expects; move the previous release into "UF2/older versions" when bumping it.
# Re-run cmake after changing the version.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/info.yaml)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/info.yaml BB_VERSION_LINE REGEX "^Version:")
string(REGEX REPLACE "^Version:[ \t]*([^ \t]+).*$" "\\1" BB_VERSION "${BB_VERSION_LINE}")
if(NOT BB_VERSION)
    message(FATAL_ERROR "No Version: line found in info.yaml")
endif()
set(BB_RELEASE_UF2 ${CARD_NAME}.${BB_VERSION}.release.uf2)

# Copy UF2 files to UF2 directory and add user script erase blocks
add_custom_command(
    TARGET ${CARD_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2/platform
    # Also copy the raw UF2 (no user-script erase blocks). Useful as a base for tools (e.g. Web-Druid)
    # that want to inject a startup script into the reserved user-script flash region.
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/${CARD_NAME}.uf2
        ${CMAKE_CURRENT_SOURCE_DIR}/UF2/platform/${CARD_NAME}_platform.uf2
    # First create a temp UF2 with erase blocks
    COMMAND ${CMAKE_COMMAND} -E env python3 
            ${CMAKE_CURRENT_SOURCE_DIR}/util/add_uf2_erase_block.py
            ${CMAKE_CURRENT_BINARY_DIR}/${CARD_NAME}.uf2
            ${CMAKE_CURRENT_SOURCE_DIR}/UF2/${BB_RELEASE_UF2}
    COMMENT "Adding user script erase blocks to UF2 (UF2/${BB_RELEASE_UF2})"
)
endif()
