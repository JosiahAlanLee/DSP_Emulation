CXX ?= c++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra
.PHONY: all test bench clean
all: core_test pvx_corrections_test regression_test
core_test: core_test.cpp ../src/engine/PvxVoice.hpp ../../shared/RateConverter.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
pvx_corrections_test: pvx_corrections_test.cpp ../src/engine/PvxVoice.hpp ../../shared/RateConverter.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
regression_test: regression_test.cpp ../src/engine/PvxVoice.hpp ../../shared/RateConverter.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
bench: bench.cpp ../src/engine/PvxVoice.hpp ../../shared/RateConverter.hpp
	$(CXX) $(CXXFLAGS) bench.cpp -o bench
	./bench
test: all
	./core_test
	./pvx_corrections_test
	./regression_test
clean:
	rm -f core_test pvx_corrections_test regression_test bench

RACK_DIR ?= /home/echevarian/dev/Rack-SDK
module_test: module_test.cpp ../src/plugin.cpp ../src/engine/PvxVoice.hpp ../src/Layout.hpp
	$(CXX) $(CXXFLAGS) $< -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include -L$(RACK_DIR) -Wl,-rpath,$(abspath $(RACK_DIR)) -lRack -Wl,--allow-shlib-undefined -o $@
module-check: module_test
	./module_test
