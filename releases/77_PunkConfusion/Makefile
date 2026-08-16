PYTHON ?= python3
WEB_PORT ?= 8765
BUILD_DIR ?= build

.PHONY: webui smoke build restore-factory-samples clean

webui:
	$(PYTHON) -m http.server $(WEB_PORT) --directory web

smoke:
	$(PYTHON) -m py_compile tools/generate_vocal_samples.py
	node --check web/app.js

build:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -j2

restore-factory-samples:
	cp factory-samples/VocalSamples.h VocalSamples.h
	cp factory-samples/samples/*.wav samples/

clean:
	rm -rf $(BUILD_DIR)
