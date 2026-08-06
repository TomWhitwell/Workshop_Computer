SHELL := /usr/bin/env bash

.PHONY: help all clean FORCE

# Directory to clean when running `make clean` (must be provided explicitly).
DIR ?=

help:
	@echo "Usage: make <directory-with-CMakeLists.txt>"
	@echo "Example: make releases/10_twists/src"
	@echo ""
	@echo "Build all compatible releases:"
	@echo "  make all                  # build every releases/* card with a CMakeLists.txt"
	@echo ""
	@echo "Cleaning:"
	@echo "  make clean DIR=<dir>      # remove <dir>/build"
	@echo ""
	@echo "Per-card builds also work from inside a card directory via the"
	@echo "auto-included scripts/pico_auto_make.mk (make / make uf2 / make flash)."

# Build all compatible Pico SDK releases (sequentially, stop on first failure)
RELEASES_DIR ?= releases

all:
	@set -euo pipefail; \
	if [[ ! -d "$(RELEASES_DIR)" ]]; then \
		echo "Error: releases directory not found: $(RELEASES_DIR)" >&2; \
		exit 2; \
	fi; \
	found=0; \
	failures=0; \
	declare -a targets=(); \
	declare -a statuses=(); \
	while IFS= read -r -d '' cmake; do \
		found=1; \
		dir=$$(dirname "$$cmake"); \
		targets+=("$$dir"); \
		echo "==> Building $$dir"; \
		log_file=$$(mktemp); \
		if (cd "$$dir" && make build) 2>&1 | tee "$$log_file"; then \
			statuses+=("✅ Success"); \
		else \
			tinyusb_hit=$$(grep -Eqi "tinyusb|\\btusb\\b" "$$log_file" && echo yes || echo no); \
			picosdk_hit=$$(grep -Eqi "pico[- ]sdk|PICO_SDK" "$$log_file" && echo yes || echo no); \
			if [[ "$$tinyusb_hit" == "yes" && "$$picosdk_hit" == "yes" ]]; then \
				statuses+=("❌ Failed (TinyUSB/Pico SDK issues)"); \
			elif [[ "$$tinyusb_hit" == "yes" ]]; then \
				statuses+=("❌ Failed (TinyUSB version mismatch)"); \
			elif [[ "$$picosdk_hit" == "yes" ]]; then \
				statuses+=("❌ Failed (Pico SDK version mismatch)"); \
			else \
				statuses+=("❌ Failed (Compile errors)"); \
			fi; \
			failures=$$((failures+1)); \
		fi; \
		rm -f "$$log_file"; \
	done < <(find "$(RELEASES_DIR)" -mindepth 2 -maxdepth 4 -type f -name CMakeLists.txt ! -path "*/lua/*" ! -path "*/pico-sdk/*" ! -path "*/ComputerCard/*" -print0 | sort -z); \
	if [[ $$found -eq 0 ]]; then \
		echo "No CMakeLists.txt found under $(RELEASES_DIR)" >&2; \
		exit 2; \
	fi; \
	total=$${#targets[@]}; \
	successes=$$((total - failures)); \
	echo ""; \
	echo "| Target path | Build status |"; \
	echo "| --- | --- |"; \
	while IFS=$$'\t' read -r target status; do \
		echo "| $$target | $$status |"; \
	done < <(for i in "$${!targets[@]}"; do printf '%s\t%s\n' "$${targets[$$i]}" "$${statuses[$$i]}"; done | sort); \
	echo ""; \
	echo "Total: $$total, Success: $$successes, Failed: $$failures"; \
	if [[ $$failures -gt 0 ]]; then \
		exit 2; \
	fi

clean:
	@if [[ -z "$(DIR)" ]]; then \
		echo "Error: DIR not set (usage: make clean DIR=<dir>)" >&2; \
		exit 2; \
	fi
	@if [[ ! -d "$(DIR)" ]]; then \
		echo "Error: directory not found: $(DIR)" >&2; \
		exit 2; \
	fi
	@rm -rf "$(DIR)/build"

# Build an arbitrary directory that contains a CMakeLists.txt.
%: FORCE
	@./scripts/build.sh "$@"

FORCE:

Makefile:
	@:
