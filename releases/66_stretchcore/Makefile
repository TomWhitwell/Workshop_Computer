export PICO_SDK_PATH := $(CURDIR)/pico-sdk

PROJECT := stretchcore
BUILD_DIR := build
PORT ?= /dev/ttyACM0
PICO_SDK_VERSION := 2.2.0
PICO_SDK_STAMP := pico-sdk/.stretchcore-sdk-$(PICO_SDK_VERSION)
NPROCS := $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
NPM := /home/zns/.nvm/versions/node/v25.8.1/bin/npm
NODE_BIN := $(dir $(NPM))

.PHONY: build upload clean pico-sdk encode web-install web-build web-dev


build: $(PICO_SDK_STAMP) $(BUILD_DIR)/Makefile
	cmake --build $(BUILD_DIR) --parallel $(NPROCS)
	test -f $(BUILD_DIR)/$(PROJECT).uf2
	mkdir -p UF2 web/public
	cp $(BUILD_DIR)/$(PROJECT).uf2 UF2/$(PROJECT).uf2
	cp $(BUILD_DIR)/$(PROJECT).uf2 web/public/$(PROJECT).uf2

upload: build
	./scripts/upload_uf2.sh "$(BUILD_DIR)/$(PROJECT).uf2" "$(PORT)"

encode: src/breaky_audio.h

src/breaky_audio.h:
	@echo "Audio is now loaded through web/ over Web Serial; src/breaky_audio.h is no longer generated."

$(BUILD_DIR)/Makefile: CMakeLists.txt pico_sdk_import.cmake src/main.cpp src/ComputerCard.h src/BreakyAudioBank.h src/BreakyAudioBank.cpp src/BreakySampleManager.h src/BreakySampleManager.cpp $(PICO_SDK_STAMP)
	cmake -S . -B $(BUILD_DIR)

pico-sdk: $(PICO_SDK_STAMP)

$(PICO_SDK_STAMP):
	@if [ ! -d pico-sdk/.git ]; then \
		git clone https://github.com/raspberrypi/pico-sdk pico-sdk; \
	fi
	cd pico-sdk && git fetch --tags --depth 1 origin $(PICO_SDK_VERSION)
	cd pico-sdk && git checkout $(PICO_SDK_VERSION)
	cd pico-sdk && git submodule update --init --recursive
	touch $(PICO_SDK_STAMP)

clean:
	rm -rf $(BUILD_DIR)
	rm -f src/breaky_audio.h

web-install:
	cd web && PATH="$(NODE_BIN):$$PATH" $(NPM) install

web-build:
	cd web && PATH="$(NODE_BIN):$$PATH" $(NPM) run build

web-dev:
	cd web && PATH="$(NODE_BIN):$$PATH" $(NPM) run dev -- --host 127.0.0.1
