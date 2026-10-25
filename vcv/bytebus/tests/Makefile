CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fno-unsafe-math-optimizations
RACK_DIR ?= ../../../../deps/Rack-SDK
SDK_FLAGS = -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include -L$(RACK_DIR) -Wl,-rpath,$(abspath $(RACK_DIR)) -lRack -Wl,--allow-shlib-undefined
HEADERS = ../src/engine/Register.hpp ../../shared/ByteBus.hpp
all: engine_test module_test
engine_test: engine_test.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@
module_test: module_test.cpp ../src/Register.cpp $(HEADERS) ../../vastish/src/Vastish.cpp ../../vastish/src/engine/*.hpp
	$(CXX) $(CXXFLAGS) $< $(SDK_FLAGS) -o $@
test: engine_test
	./engine_test
check: all
	./engine_test
	./module_test
sanitize:
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer engine_test.cpp -o engine_asan
	./engine_asan
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer module_test.cpp $(SDK_FLAGS) -o module_asan
	ASAN_OPTIONS=detect_leaks=0 ./module_asan
clean:
	rm -f engine_test module_test engine_asan module_asan
.PHONY: all test check sanitize clean
