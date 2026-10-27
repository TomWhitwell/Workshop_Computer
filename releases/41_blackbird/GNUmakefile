# Blackbird — local Makefile.
#
# Usage:
#   make firmware      Build release firmware (UF2/blackbird.<version>.release.uf2)
#   make debug         Build the SWD debug/test harness firmware (build-debug/blackbird_debug.elf)
#   make flash         Build + flash release firmware over SWD (OpenOCD)
#   make flash-debug   Build + flash debug harness firmware over SWD
#   make test          Run tests/ against a card running the debug firmware
#   make test-bowery   Run only the bbbowery + monome/bowery script tests
#   make test-norns    Run only the norns tests (NORNS_HOST=<norns address>)
#   make audit         Check nothing core 1 runs lives in flash (both builds)
#   make host-test     Run host-side unit tests (no card needed)
#   make clean         Remove build directories
#
# OpenOCD runs on the host: override OPENOCD_HOST / OPENOCD_GDB_PORT if needed.
# See docs/DEBUG_HARNESS.md.

SHELL := /bin/bash

PICO_SDK_PATH ?= /opt/pico-sdk
CMAKE_ARGS := -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico -DPICO_SDK_PATH=$(PICO_SDK_PATH)

BUILD := build
BUILD_DEBUG := build-debug
OPENOCD_HOST     ?= host.docker.internal
OPENOCD_GDB_PORT ?= 3333
GDB ?= $(shell command -v arm-none-eabi-gdb 2>/dev/null || command -v gdb-multiarch 2>/dev/null)
TEST_ARGS ?=

.PHONY: all build firmware debug flash flash-debug test test-bowery test-norns audit host-test clean help

all: firmware

# Alias so the root `make all` (which invokes `make build` per card) works here.
build: firmware

firmware:
	@mkdir -p $(BUILD)
	@cd $(BUILD) && cmake .. $(CMAKE_ARGS) -DBB_DEBUG_HARNESS=OFF
	@cd $(BUILD) && ninja

debug:
	@mkdir -p $(BUILD_DEBUG)
	@cd $(BUILD_DEBUG) && cmake .. $(CMAKE_ARGS) -DBB_DEBUG_HARNESS=ON
	@cd $(BUILD_DEBUG) && ninja

define gdb_flash
	@$(GDB) -q -nx -batch \
		-ex "target extended-remote $(OPENOCD_HOST):$(OPENOCD_GDB_PORT)" \
		-ex "monitor reset init" \
		-ex "load" \
		-ex "monitor reset init" \
		-ex "monitor reset run" \
		-ex "detach" \
		$(1)
	@echo "FLASH OK"
endef

flash: firmware
	@echo "Flashing blackbird..."
	$(call gdb_flash,$(BUILD)/blackbird.elf)

flash-debug: debug
	@echo "Flashing blackbird debug harness..."
	$(call gdb_flash,$(BUILD_DEBUG)/blackbird_debug.elf)

test: debug
	@python3 util/bbdbg.py --host $(OPENOCD_HOST) --port $(OPENOCD_GDB_PORT) test $(TEST_ARGS)

test-bowery: debug
	@python3 util/bbdbg.py --host $(OPENOCD_HOST) --port $(OPENOCD_GDB_PORT) test tests/test_bowery.py $(TEST_ARGS)

test-norns: debug
	@python3 util/bbdbg.py --host $(OPENOCD_HOST) --port $(OPENOCD_GDB_PORT) test tests/test_norns.py $(TEST_ARGS)

audit: firmware debug
	@python3 util/core1_flash_audit.py $(BUILD)/blackbird.elf | tail -1
	@python3 util/core1_flash_audit.py $(BUILD_DEBUG)/blackbird_debug.elf | tail -1

host-test:
	@mkdir -p build-host
	@cc -O1 -g -fsanitize=address,undefined -Wall -Wextra -Ilib -o build-host/test_lua_pool tests/host/test_lua_pool.c lib/lua_pool.c
	@./build-host/test_lua_pool

clean:
	@rm -rf $(BUILD) $(BUILD_DEBUG) build-host

help:
	@sed -n '3,16p' GNUmakefile
