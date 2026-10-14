CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
.PHONY: all test sanitize window-sanitize thread-sanitize clean
all: build/engine_test build/render build/snapshot_test build/lab_test build/window_test build/mutation_concurrency_test
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
test: all
	cd .. && tests/build/engine_test
	cd .. && tests/build/snapshot_test
	cd .. && tests/build/lab_test
	cd .. && tests/build/window_test
	cd .. && tests/build/mutation_concurrency_test
sanitize: | build
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer engine_test.cpp -o build/engine_test_sanitized
	cd .. && ASAN_OPTIONS=detect_leaks=1 tests/build/engine_test_sanitized
window-sanitize: | build
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer window_test.cpp -o build/window_test_sanitized
	cd .. && ASAN_OPTIONS=detect_leaks=1 tests/build/window_test_sanitized
thread-sanitize: | build
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=thread -fno-omit-frame-pointer snapshot_test.cpp -o build/snapshot_test_tsan
	cd .. && tests/build/snapshot_test_tsan
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=thread -fno-omit-frame-pointer mutation_concurrency_test.cpp -o build/mutation_concurrency_tsan
	cd .. && tests/build/mutation_concurrency_tsan
clean:
	rm -rf build
