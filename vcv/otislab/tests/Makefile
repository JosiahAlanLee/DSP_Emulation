CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
.PHONY: all test sanitize window-sanitize thread-sanitize clean
all: build/engine_test build/render build/snapshot_test build/lab_test build/window_test build/mutation_concurrency_test build/stereo_test build/frame_bus_test build/kit_test build/kit_capture_test
build:
	mkdir -p build
build/engine_test: engine_test.cpp ../src/engine/OtisEngine.hpp ../src/engine/Wav.hpp | build
	$(CXX) $(CXXFLAGS) engine_test.cpp -o $@
build/render: render.cpp ../src/engine/OtisEngine.hpp ../src/engine/Wav.hpp | build
	$(CXX) $(CXXFLAGS) render.cpp -o $@
build/lab_test: lab_test.cpp ../src/engine/OtisEngine.hpp | build
	$(CXX) $(CXXFLAGS) lab_test.cpp -o $@
build/snapshot_test: snapshot_test.cpp ../src/engine/OtisEngine.hpp | build
	$(CXX) $(CXXFLAGS) snapshot_test.cpp -o $@
build/window_test: window_test.cpp ../src/engine/LoopMutator.hpp ../src/engine/OtisEngine.hpp | build
	$(CXX) $(CXXFLAGS) -Wno-mismatched-new-delete window_test.cpp -o $@
build/mutation_concurrency_test: mutation_concurrency_test.cpp ../src/engine/LoopMutator.hpp ../src/engine/OtisEngine.hpp | build
	$(CXX) $(CXXFLAGS) mutation_concurrency_test.cpp -o $@
build/stereo_test: stereo_test.cpp ../src/engine/LoopMutator.hpp ../src/engine/OtisEngine.hpp ../src/engine/Wav.hpp | build
	$(CXX) $(CXXFLAGS) stereo_test.cpp -o $@
build/kit_test: kit_test.cpp ../src/engine/Kit.hpp ../src/engine/OtisEngine.hpp | build
	$(CXX) $(CXXFLAGS) kit_test.cpp -o $@
build/kit_capture_test: kit_capture_test.cpp ../src/engine/Kit.hpp ../src/engine/OtisEngine.hpp | build
	$(CXX) $(CXXFLAGS) kit_capture_test.cpp -o $@
build/frame_bus_test: frame_bus_test.cpp ../src/engine/OtisEngine.hpp ../src/engine/FrameTable.hpp ../../shared/ByteBus.hpp | build
	$(CXX) $(CXXFLAGS) -Wno-mismatched-new-delete $< -o $@
RACK_DIR ?= ../../../../deps/Rack-SDK
SDK_FLAGS = -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include -L$(RACK_DIR) -Wl,-rpath,$(abspath $(RACK_DIR)) -lRack -Wl,--allow-shlib-undefined
build/frame_bus_module_test: frame_bus_module_test.cpp ../src/OtisSampler.cpp ../src/engine/*.hpp ../../shared/*.hpp ../../bytebus/src/*.cpp ../../bytebus/src/engine/*.hpp | build
	$(CXX) $(CXXFLAGS) -Wno-unused-parameter -fno-unsafe-math-optimizations $< $(SDK_FLAGS) -o $@
build/patch_select_module_test: patch_select_module_test.cpp ../src/OtisSampler.cpp ../src/engine/*.hpp ../../shared/*.hpp | build
	$(CXX) $(CXXFLAGS) -Wno-unused-parameter -fno-unsafe-math-optimizations $< $(SDK_FLAGS) -o $@
build/kit_module_test: kit_module_test.cpp ../src/OtisKit.cpp ../src/OtisCapture.cpp ../src/KitLayout.hpp ../src/engine/*.hpp ../../shared/*.hpp | build
	$(CXX) $(CXXFLAGS) -Wno-unused-parameter -fno-unsafe-math-optimizations $< $(SDK_FLAGS) -o $@
build/capture_module_test: capture_module_test.cpp ../src/OtisKit.cpp ../src/OtisCapture.cpp ../src/CaptureLink.hpp ../src/KitLayout.hpp ../src/engine/*.hpp ../../shared/*.hpp | build
	$(CXX) $(CXXFLAGS) -Wno-unused-parameter -fno-unsafe-math-optimizations $< $(SDK_FLAGS) -o $@
bus-check: build/sampler_thread_test build/frame_bus_module_test build/patch_select_module_test build/kit_module_test build/capture_module_test
	cd .. && tests/build/sampler_thread_test
	cd .. && tests/build/frame_bus_module_test
	cd .. && tests/build/patch_select_module_test
	cd .. && tests/build/kit_module_test
	cd .. && tests/build/capture_module_test
bus-sanitize: | build
	$(CXX) $(CXXFLAGS) -O1 -g -Wno-mismatched-new-delete -fsanitize=address,undefined -fno-omit-frame-pointer frame_bus_test.cpp -o build/frame_bus_asan
	cd .. && ASAN_OPTIONS=detect_leaks=0 tests/build/frame_bus_asan
	$(CXX) $(CXXFLAGS) -O1 -g -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer frame_bus_module_test.cpp $(SDK_FLAGS) -o build/frame_bus_module_asan
	cd .. && ASAN_OPTIONS=detect_leaks=0 tests/build/frame_bus_module_asan
	$(CXX) $(CXXFLAGS) -O1 -g -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer patch_select_module_test.cpp $(SDK_FLAGS) -o build/patch_select_module_asan
	cd .. && ASAN_OPTIONS=detect_leaks=0 tests/build/patch_select_module_asan
	$(CXX) $(CXXFLAGS) -O1 -g -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer kit_module_test.cpp $(SDK_FLAGS) -o build/kit_module_asan
	cd .. && ASAN_OPTIONS=detect_leaks=0 tests/build/kit_module_asan
	$(CXX) $(CXXFLAGS) -O1 -g -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer capture_module_test.cpp $(SDK_FLAGS) -o build/capture_module_asan
	cd .. && ASAN_OPTIONS=detect_leaks=0 tests/build/capture_module_asan
test: all
	cd .. && tests/build/engine_test
	cd .. && tests/build/snapshot_test
	cd .. && tests/build/lab_test
	cd .. && tests/build/window_test
	cd .. && tests/build/mutation_concurrency_test
	cd .. && tests/build/stereo_test
	cd .. && tests/build/frame_bus_test
	cd .. && tests/build/kit_test
	cd .. && tests/build/kit_capture_test
sanitize: | build
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer engine_test.cpp -o build/engine_test_sanitized
	cd .. && ASAN_OPTIONS=detect_leaks=1 tests/build/engine_test_sanitized
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer stereo_test.cpp -o build/stereo_test_sanitized
	cd .. && ASAN_OPTIONS=detect_leaks=1 tests/build/stereo_test_sanitized
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer kit_test.cpp -o build/kit_test_sanitized
	cd .. && ASAN_OPTIONS=detect_leaks=1 tests/build/kit_test_sanitized
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer kit_capture_test.cpp -o build/kit_capture_test_sanitized
	cd .. && ASAN_OPTIONS=detect_leaks=1 tests/build/kit_capture_test_sanitized
window-sanitize: | build
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer window_test.cpp -o build/window_test_sanitized
	cd .. && ASAN_OPTIONS=detect_leaks=1 tests/build/window_test_sanitized
thread-sanitize: sampler-thread-sanitize | build
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=thread -fno-omit-frame-pointer snapshot_test.cpp -o build/snapshot_test_tsan
	cd .. && tests/build/snapshot_test_tsan
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=thread -fno-omit-frame-pointer mutation_concurrency_test.cpp -o build/mutation_concurrency_tsan
	cd .. && tests/build/mutation_concurrency_tsan
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=thread -fno-omit-frame-pointer kit_capture_test.cpp -o build/kit_capture_tsan
	cd .. && tests/build/kit_capture_tsan
clean:
	rm -rf build
.PHONY: bus-check bus-sanitize

build/sampler_thread_test: sampler_thread_test.cpp ../src/OtisSampler.cpp ../src/engine/*.hpp ../../shared/*.hpp | build
	$(CXX) $(CXXFLAGS) -Wno-unused-parameter $< $(SDK_FLAGS) -o $@
sampler-thread-sanitize: | build
	$(CXX) $(CXXFLAGS) -O1 -g -Wno-unused-parameter -fsanitize=thread -fno-omit-frame-pointer sampler_thread_test.cpp $(SDK_FLAGS) -o build/sampler_thread_tsan
	cd .. && tests/build/sampler_thread_tsan
.PHONY: sampler-thread-sanitize
