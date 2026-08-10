# Global Makefile include for this dev environment.
#
# This file is injected via the MAKEFILES environment variable (see
# .devcontainer/devcontainer.json). It enables a convenient workflow:
#
#   cd Workshop_Computer/releases/<card>
#   make
#
# even when that directory contains no local Makefile, as long as it looks like
# a Pico SDK (CMake) project.

SHELL := /bin/bash

# Absolute path to this harness file and the dev-env repo root that contains it.
# Captured here (rather than hardcoded) so the harness works regardless of the
# workspace folder name or where the repo is cloned.
PICO_AUTO_MAKE_MK := $(abspath $(lastword $(MAKEFILE_LIST)))
DEV_ENV_ROOT := $(abspath $(dir $(PICO_AUTO_MAKE_MK))/..)

# Only activate when there is NO local makefile in the current directory.
LOCAL_MAKEFILE := $(firstword $(wildcard GNUmakefile Makefile makefile))
ifeq ($(strip $(LOCAL_MAKEFILE)),)

# Detect where the CMake project is rooted.
# Preference order:
#  1) ./CMakeLists.txt
#  2) ./src/CMakeLists.txt
#  3) first CMakeLists.txt within depth 2 (excluding build-like dirs)
PICO_PROJECT_DIR := $(strip \
  $(if $(wildcard CMakeLists.txt),.,\
    $(if $(wildcard src/CMakeLists.txt),src,\
      $(shell find . -maxdepth 2 -mindepth 2 -name CMakeLists.txt \
        -not -path './build/*' -not -path './*/build/*' \
        -not -path './_build/*' -not -path './*/_build/*' \
        -not -path './cmake-build-*/*' -not -path './*/cmake-build-*/*' \
        -print -quit 2>/dev/null | sed 's#^\\./##' | xargs -r dirname))))

PICO_PROJECT_CMAKELISTS := $(if $(PICO_PROJECT_DIR),$(PICO_PROJECT_DIR)/CMakeLists.txt,)

# Only treat as Pico SDK project if it either has a pico_sdk_import.cmake alongside
# the CMakeLists.txt or references pico_sdk_import from the CMakeLists.
PICO_IS_PICO_SDK := $(strip \
  $(if $(PICO_PROJECT_DIR),\
    $(shell \
      if [[ -f "$(PICO_PROJECT_DIR)/pico_sdk_import.cmake" ]]; then echo yes; \
      elif [[ -f "$(PICO_PROJECT_CMAKELISTS)" ]] && grep -q "pico_sdk_import" "$(PICO_PROJECT_CMAKELISTS)"; then echo yes; \
      else echo no; fi \
    ),\
    no))

ifeq ($(PICO_IS_PICO_SDK),yes)

.DEFAULT_GOAL := all

CMAKE ?= cmake
BUILD_DIR ?= build
CMAKE_CXX_STANDARD ?= 17

# Pico SDK resolution:
# - Respect PICO_SDK_PATH if already set.
# - Otherwise prefer the SDK in this dev container.
# - Otherwise fall back to the SDK built under ComputerCard_Examples (if present).
# - Otherwise allow FetchContent via Pico SDK's standard variables.
PICO_SDK_PATH ?= $(if $(wildcard /opt/pico-sdk/pico_sdk_init.cmake),/opt/pico-sdk,)
WORKSPACE_PICO_SDK ?= $(DEV_ENV_ROOT)/ComputerCard_Examples/build/pico-sdk
ifneq ($(strip $(PICO_SDK_PATH)),)
  CMAKE_PICO_SDK_ARGS := -DPICO_SDK_PATH=$(PICO_SDK_PATH)
else ifneq ($(wildcard $(WORKSPACE_PICO_SDK)/pico_sdk_init.cmake),)
  CMAKE_PICO_SDK_ARGS := -DPICO_SDK_PATH=$(WORKSPACE_PICO_SDK)
else
  PICO_SDK_FETCH_FROM_GIT ?= ON
  PICO_SDK_FETCH_FROM_GIT_PATH ?= $(HOME)/.pico-sdk
  PICO_SDK_FETCH_FROM_GIT_TAG ?= master
  CMAKE_PICO_SDK_ARGS := \
    -DPICO_SDK_FETCH_FROM_GIT=$(PICO_SDK_FETCH_FROM_GIT) \
    -DPICO_SDK_FETCH_FROM_GIT_PATH=$(PICO_SDK_FETCH_FROM_GIT_PATH) \
    -DPICO_SDK_FETCH_FROM_GIT_TAG=$(PICO_SDK_FETCH_FROM_GIT_TAG)
endif

CMAKE_ARGS ?=
PICO_BOARD_HEADER_DIRS ?= $(abspath $(PICO_PROJECT_DIR));$(abspath $(PICO_PROJECT_DIR)/..)

# Toolchain resolution:
# - Respect PICO_TOOLCHAIN_PATH if already set.
# - Otherwise infer from arm-none-eabi-gcc on PATH.
TOOLCHAIN_C := $(strip $(shell command -v arm-none-eabi-gcc 2>/dev/null))
TOOLCHAIN_CXX := $(strip $(shell command -v arm-none-eabi-g++ 2>/dev/null))
ifneq ($(TOOLCHAIN_C),)
	TOOLCHAIN_ROOT := $(abspath $(dir $(TOOLCHAIN_C))/..)
	PICO_TOOLCHAIN_PATH ?= $(TOOLCHAIN_ROOT)
endif

CMAKE_ARGS += -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(CMAKE_PICO_SDK_ARGS) \
	-DCMAKE_CXX_STANDARD=$(CMAKE_CXX_STANDARD) \
	-DCMAKE_CXX_STANDARD_REQUIRED=ON \
	-DPICO_BOARD_HEADER_DIRS="$(PICO_BOARD_HEADER_DIRS)"

# Place UF2 artifacts alongside the project CMakeLists.txt
UF2_DIR ?= $(PICO_PROJECT_DIR)/UF2

ifneq ($(strip $(PICO_TOOLCHAIN_PATH)),)
  CMAKE_ARGS += -DPICO_TOOLCHAIN_PATH=$(PICO_TOOLCHAIN_PATH)
endif

ifneq ($(strip $(TOOLCHAIN_C)),)
	CMAKE_ARGS += -DCMAKE_C_COMPILER=$(TOOLCHAIN_C)
endif
ifneq ($(strip $(TOOLCHAIN_CXX)),)
	CMAKE_ARGS += -DCMAKE_CXX_COMPILER=$(TOOLCHAIN_CXX)
endif

NINJA := $(shell command -v ninja 2>/dev/null)
ifneq ($(strip $(NINJA)),)
  CMAKE_GEN_ARGS := -G "Ninja"
else
  CMAKE_GEN_ARGS :=
endif

.PHONY: all configure build clean distclean uf2 help .

# Support the (somewhat common) "make ." habit.
.: all

all: build

configure:
	$(CMAKE) -S "$(PICO_PROJECT_DIR)" -B "$(BUILD_DIR)" $(CMAKE_GEN_ARGS) $(CMAKE_ARGS)

build:
	@set -uo pipefail; \
	  run_build() { \
	    "$(CMAKE)" -S "$(PICO_PROJECT_DIR)" -B "$(BUILD_DIR)" $(CMAKE_GEN_ARGS) $(CMAKE_ARGS) || return $$?; \
	    "$(CMAKE)" --build "$(BUILD_DIR)" || return $$?; \
	    mkdir -p "$(UF2_DIR)"; \
	    shopt -s nullglob; \
	    found=0; \
	    while IFS= read -r -d '' f; do \
	      found=1; \
	      cp -f "$$f" "$(UF2_DIR)/"; \
	      cwd="$$(pwd)"; \
	      rel="$$cwd"; \
	      rel="$${rel#/workspaces/}"; \
	      dest="$$(cd "$(PICO_PROJECT_DIR)" && pwd)"; \
	      dest="$${dest#/workspaces/}/UF2/$$(basename "$$f")"; \
	      echo "UF2 copied to $$dest"; \
	    done < <(find "$(BUILD_DIR)" -maxdepth 4 -type f -name '*.uf2' -print0 2>/dev/null); \
	    build_abs="$$(cd "$(BUILD_DIR)" && pwd)"; \
	    last_elf="$$(find "$$build_abs" -maxdepth 4 -type f -name '*.elf' -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2-)"; \
	    if [[ -n "$$last_elf" ]]; then \
	      ln -sf "$$last_elf" "$(BUILD_DIR)/.last.elf"; \
	    fi; \
	    if [[ $$found -eq 0 ]]; then \
	      true; \
	    fi; \
	  }; \
	  if run_build; then \
	    exit 0; \
	  fi; \
	  echo "Build failed; cleaning and retrying once..."; \
	  rm -rf "$(BUILD_DIR)"; \
	  run_build

clean:
	rm -rf "$(BUILD_DIR)"

distclean: clean

uf2: build
	@find "$(UF2_DIR)" -maxdepth 1 -type f -name '*.uf2' -print 2>/dev/null || true
	@find "$(BUILD_DIR)" -maxdepth 3 -type f -name '*.uf2' -print 2>/dev/null || true

# Flash via a running OpenOCD instance (typically on the host).
#
# Default expects OpenOCD is already running and listening for GDB on 3333:
#   openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000"
#
# If you run OpenOCD elsewhere, override:
#   make flash OPENOCD_HOST=... OPENOCD_GDB_PORT=3333

OPENOCD_HOST ?= host.docker.internal
OPENOCD_GDB_PORT ?= 3333

# Optional: explicitly choose an ELF to load.
FLASH_ELF ?=

# Optional: explicitly choose a UF2 (used to select which target to flash).
# Note: OpenOCD/GDB flashes using an ELF, not a UF2.
FLASH_UF2 ?=

# GDB to use for flashing. If empty, auto-detect.
GDB ?=


.PHONY: flash

flash: uf2
	@set -uo pipefail; \
	  fail() { echo "FLASH FAILED: $$*"; exit 2; }; \
	  uf2="$(FLASH_UF2)"; \
	  if [[ -z "$$uf2" && -d "$(UF2_DIR)" ]]; then \
	    uf2="$$(find "$(UF2_DIR)" -maxdepth 1 -type f -name '*.uf2' -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2-)"; \
	  fi; \
	  if [[ -z "$$uf2" ]]; then \
	    uf2="$$(find "$(BUILD_DIR)" -maxdepth 4 -type f -name '*.uf2' -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2-)"; \
	  fi; \
	  stem=""; \
	  if [[ -n "$$uf2" ]]; then \
	    stem="$$(basename "$$uf2" .uf2)"; \
	  fi; \
	  elf="$(FLASH_ELF)"; \
	  if [[ -z "$$elf" && -f "$(BUILD_DIR)/.last.elf" ]]; then \
	    elf="$(BUILD_DIR)/.last.elf"; \
	  fi; \
	  if [[ -z "$$elf" && -n "$$stem" && -f "$(BUILD_DIR)/$$stem.elf" ]]; then \
	    elf="$(BUILD_DIR)/$$stem.elf"; \
	  fi; \
	  if [[ -z "$$elf" ]]; then \
	    elf="$$(find "$(BUILD_DIR)" -maxdepth 4 -type f -name '*.elf' -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2-)"; \
	  fi; \
	  if [[ -z "$$elf" || ! -f "$$elf" ]]; then \
	    fail "couldn't find an .elf under $(BUILD_DIR) (set FLASH_ELF=... or ensure build produced one)"; \
	  fi; \
	  if [[ -n "$$uf2" ]]; then \
	    echo "Using UF2 $$uf2 (selects ELF $$elf)"; \
	  fi; \
	  if ! (echo >"/dev/tcp/$(OPENOCD_HOST)/$(OPENOCD_GDB_PORT)") >/dev/null 2>&1; then \
	    echo "Start OpenOCD on the host, e.g.:" >&2; \
	    echo "  openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c \"adapter speed 5000\"" >&2; \
	    fail "cannot connect to OpenOCD GDB server at $(OPENOCD_HOST):$(OPENOCD_GDB_PORT)"; \
	  fi; \
	  gdb_cmd="$(GDB)"; \
	  if [[ -z "$$gdb_cmd" ]]; then \
	    if command -v arm-none-eabi-gdb >/dev/null 2>&1; then gdb_cmd="arm-none-eabi-gdb"; \
	    elif command -v gdb-multiarch >/dev/null 2>&1; then gdb_cmd="gdb-multiarch"; \
	    else \
	      echo "Install a GDB in the devcontainer (recommended: gdb-multiarch)." >&2; \
	      fail "no suitable GDB found in PATH (arm-none-eabi-gdb or gdb-multiarch)"; \
	    fi; \
	  fi; \
	  echo "Flashing $$elf via OpenOCD at $(OPENOCD_HOST):$(OPENOCD_GDB_PORT)"; \
	  rc=0; \
	  for attempt in 1 2; do \
	    if ! (echo >"/dev/tcp/$(OPENOCD_HOST)/$(OPENOCD_GDB_PORT)") >/dev/null 2>&1; then \
	      if [[ $$attempt -eq 1 ]]; then \
	        echo "Can't connect to OpenOCD at $(OPENOCD_HOST):$(OPENOCD_GDB_PORT); retrying once..." >&2; \
	        sleep 1; \
	        continue; \
	      fi; \
	      echo "Start OpenOCD on the host, e.g.:" >&2; \
	      echo "  openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c \"adapter speed 5000\"" >&2; \
	      fail "cannot connect to OpenOCD GDB server at $(OPENOCD_HOST):$(OPENOCD_GDB_PORT)"; \
	    fi; \
	    rc=0; \
	    "$$gdb_cmd" -q -nx -batch \
	      -ex "target extended-remote $(OPENOCD_HOST):$(OPENOCD_GDB_PORT)" \
	      -ex "monitor reset init" \
	      -ex "load" \
	      -ex "compare-sections" \
	      -ex "monitor reset run" \
	      -ex "detach" \
	      "$$elf" || rc=$$?; \
	    if [[ $$rc -eq 0 ]]; then \
	      echo "FLASH OK"; \
	      exit 0; \
	    fi; \
	    if [[ $$attempt -eq 1 ]]; then \
	      echo "FLASH FAILED (gdb exit $$rc); retrying once..."; \
	      sleep 0.5; \
	    fi; \
	  done; \
	  echo "FLASH FAILED (gdb exit $$rc)"; \
	  exit $$rc

help:
	@echo "PicoSDK helper (auto-enabled when no Makefile exists)"
	@echo "Targets: build (default), clean, uf2, flash"
	@echo "Vars: BUILD_DIR=build CMAKE=cmake PICO_SDK_PATH=/path/to/pico-sdk"
	@echo "      PICO_SDK_FETCH_FROM_GIT=ON PICO_SDK_FETCH_FROM_GIT_PATH=\$$HOME/.pico-sdk"
	@echo "      OPENOCD_HOST=host.docker.internal OPENOCD_GDB_PORT=3333 GDB=gdb-multiarch"
	@echo "      FLASH_ELF=path/to.elf FLASH_UF2=UF2/name.uf2"

endif # PICO_IS_PICO_SDK
endif # no local makefile
