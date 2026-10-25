CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fno-unsafe-math-optimizations
RACK_DIR ?= ../../../../deps/Rack-SDK
RACK_TEST_LDFLAGS = -Wl,--allow-shlib-undefined
SDK_FLAGS = -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include -L$(RACK_DIR) -Wl,-rpath,$(abspath $(RACK_DIR)) -lRack $(RACK_TEST_LDFLAGS)
all: engine_test bridge_test
engine_test: engine_test.cpp ../src/engine/*.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
bridge_test: bridge_test.cpp ../src/engine/*.hpp
	$(CXX) $(CXXFLAGS) -Wno-mismatched-new-delete $< -o $@
module_test: module_test.cpp ../src/Seance.cpp ../src/Expanders.cpp ../src/Bridge.cpp ../src/Chain.hpp ../src/engine/*.hpp ../src/Layout.hpp ../../shared/ByteBus.hpp
	$(CXX) $(CXXFLAGS) -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include $< -L$(RACK_DIR) -Wl,-rpath,$(abspath $(RACK_DIR)) -lRack $(RACK_TEST_LDFLAGS) -o $@
bridge_module_test: bridge_module_test.cpp ../src/Seance.cpp ../src/Expanders.cpp ../src/Bridge.cpp ../src/Chain.hpp ../src/engine/*.hpp ../src/Layout.hpp ../../shared/*.hpp ../../bytebus/src/Register.cpp ../../bytebus/src/Lookup.cpp ../../bytebus/src/engine/*.hpp ../../otislab/src/OtisSampler.cpp ../../otislab/src/engine/*.hpp
	$(CXX) $(CXXFLAGS) -pthread $< $(SDK_FLAGS) -o $@
bridge-check: bridge_test bridge_module_test
	./bridge_test
	./bridge_module_test
bridge-sanitize:
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-unsafe-math-optimizations bridge_test.cpp -o bridge_asan
	ASAN_OPTIONS=detect_leaks=0 ./bridge_asan
	$(CXX) -std=c++17 -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -fno-unsafe-math-optimizations bridge_module_test.cpp $(SDK_FLAGS) -o bridge_module_asan
	ASAN_OPTIONS=detect_leaks=0 ./bridge_module_asan
clean:
	rm -f engine_test module_test bridge_test bridge_module_test bridge_asan bridge_module_asan
.PHONY: all clean bridge-check bridge-sanitize
