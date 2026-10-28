RACK_DIR ?= ../../../deps/Rack-SDK
CORE_DIR ?= $(CURDIR)/_deps/machinemodule
BUILD_DIR ?= build
CMAKE ?= cmake
CMAKE_ARGS ?=
.PHONY: all deps test clean dist source
all: deps
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DRACK_DIR="$(abspath $(RACK_DIR))" -DCORE_DIR="$(abspath $(CORE_DIR))" $(CMAKE_ARGS)
	$(CMAKE) --build $(BUILD_DIR) --target plugin -j2
deps:
	./tools/fetch-core.sh "$(CORE_DIR)"
test: all
	$(CMAKE) --build $(BUILD_DIR) --target control_test renderer_test worker_test firmware_test module_test plugin_load_test jit_test mmu_test -j2
	$(CMAKE) -E env LD_LIBRARY_PATH=$(abspath $(RACK_DIR)) $(CMAKE) --build $(BUILD_DIR) --target test
source:
	python3 tools/source_bundle.py "$(CORE_DIR)"
dist: test source
	python3 tools/package.py
clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR) plugin.so plugin.dylib plugin.dll dist
