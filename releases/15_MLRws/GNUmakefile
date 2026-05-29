# MLRws — local Makefile for the dual-mono build.
#
# Usage:
#   make firmware      Build firmware (MLRws.uf2)
#   make monobright    Build 8x8 binary-grid test firmware
#   make all           Build dual-mono firmware
#   make flash         Build + flash firmware
#   make clean         Remove build directory

SHELL := /bin/bash

PICO_SDK_PATH ?= /opt/pico-sdk
CMAKE_ARGS := -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico -DPICO_SDK_PATH=$(PICO_SDK_PATH)

BUILD := build
BUILD_MONOBRIGHT := build-monobright
OPENOCD_HOST     ?= host.docker.internal
OPENOCD_GDB_PORT ?= 3333
GDB ?= $(shell command -v arm-none-eabi-gdb 2>/dev/null || command -v gdb-multiarch 2>/dev/null)

.PHONY: all firmware monobright flash flash-monobright clean help

all: firmware

firmware:
	@mkdir -p $(BUILD)
	@cd $(BUILD) && cmake .. $(CMAKE_ARGS) -DCARD_NAME=MLRws
	@cd $(BUILD) && ninja -j4

monobright:
	@mkdir -p $(BUILD_MONOBRIGHT)
	@cd $(BUILD_MONOBRIGHT) && cmake .. $(CMAKE_ARGS) -DCARD_NAME=MLRws_monobright -DMONOME_WS_FORCE_8X8_MONOBRIGHT=ON
	@cd $(BUILD_MONOBRIGHT) && ninja -j4

flash: firmware
	@echo "Flashing MLRws..."
	@$(GDB) -q -nx -batch \
		-ex "target extended-remote $(OPENOCD_HOST):$(OPENOCD_GDB_PORT)" \
		-ex "monitor reset init" \
		-ex "load" \
		-ex "monitor reset run" \
		-ex "detach" \
		$(BUILD)/MLRws.elf
	@echo "FLASH OK"

flash-monobright: monobright
	@echo "Flashing MLRws monobright test build..."
	@$(GDB) -q -nx -batch \
		-ex "target extended-remote $(OPENOCD_HOST):$(OPENOCD_GDB_PORT)" \
		-ex "monitor reset init" \
		-ex "load" \
		-ex "monitor reset run" \
		-ex "detach" \
		$(BUILD_MONOBRIGHT)/MLRws_monobright.elf
	@echo "FLASH OK"

clean:
	@rm -rf $(BUILD) $(BUILD_MONOBRIGHT)

help:
	@echo "MLRws build targets:"
	@echo "  make firmware      Build firmware (MLRws.uf2)"
	@echo "  make monobright    Build 8x8 binary-grid test firmware"
	@echo "  make all           Build firmware"
	@echo "  make flash         Build + flash firmware"
	@echo "  make flash-monobright Build + flash 8x8 binary-grid test firmware"
	@echo "  make clean         Remove build directory"
