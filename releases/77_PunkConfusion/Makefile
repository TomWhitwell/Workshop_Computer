PYTHON ?= python3
WEB_PORT ?= 8765
BUILD_DIR ?= build-custom
CUSTOM_UF2 ?= uf2/punk_confusion_custom.uf2

.PHONY: webui smoke custom-uf2 restore-factory-samples clean-web

webui:
	$(PYTHON) tools/web_uf2_server.py $(WEB_PORT)

smoke:
	$(PYTHON) -m py_compile tools/generate_vocal_samples.py tools/build_custom_uf2.py tools/web_uf2_server.py
	node --check web/app.js

custom-uf2:
	$(PYTHON) tools/build_custom_uf2.py --clean --build-dir $(BUILD_DIR) --output $(CUSTOM_UF2)

restore-factory-samples:
	cp factory-samples/VocalSamples.h VocalSamples.h
	cp factory-samples/samples/*.wav samples/

clean-web:
	rm -rf build-web
