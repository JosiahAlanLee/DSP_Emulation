CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fno-unsafe-math-optimizations
RACK_DIR ?= ../../../../deps/Rack-SDK
RACK_TEST_LDFLAGS = -Wl,--allow-shlib-undefined
all: engine_test
engine_test: engine_test.cpp ../src/engine/*.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
capture_race_test: capture_race_test.cpp ../src/engine/*.hpp
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=thread -pthread $< -o $@
module_test: module_test.cpp ../src/Espish.cpp ../src/engine/*.hpp ../src/Layout.hpp ../../shared/SampleShare.hpp
	$(CXX) $(CXXFLAGS) -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include $< -L$(RACK_DIR) -Wl,-rpath,$(abspath $(RACK_DIR)) -lRack $(RACK_TEST_LDFLAGS) -o $@
clean:
	rm -f engine_test module_test capture_race_test
