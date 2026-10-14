CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic
.PHONY: all test sanitize clean
all: build/engine_test build/render
build:
	mkdir -p build
build/engine_test: engine_test.cpp ../src/engine/OtisEngine.hpp ../src/engine/Wav.hpp | build
	$(CXX) $(CXXFLAGS) engine_test.cpp -o $@
build/render: render.cpp ../src/engine/OtisEngine.hpp ../src/engine/Wav.hpp | build
	$(CXX) $(CXXFLAGS) render.cpp -o $@
test: all
	cd .. && tests/build/engine_test
sanitize: | build
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer engine_test.cpp -o build/engine_test_sanitized
	cd .. && ASAN_OPTIONS=detect_leaks=1 tests/build/engine_test_sanitized
clean:
	rm -rf build
