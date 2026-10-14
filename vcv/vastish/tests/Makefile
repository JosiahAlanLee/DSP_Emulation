CXX ?= g++
CC ?= gcc
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-mismatched-new-delete
RACK_DIR ?= ../../../../deps/Rack-SDK
PD_DIR ?= ../../../../pd-reference
all: engine_test plugin_test adapter_test render
engine_test: engine_test.cpp ../src/engine/*.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
render: render.cpp ../src/engine/*.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
plugin_test: plugin_test.cpp ../src/engine/*.hpp
	$(CXX) $(CXXFLAGS) -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include $< -L$(RACK_DIR) -Wl,-rpath,$(abspath $(RACK_DIR)) -lRack -ldl -o $@
reference_%.o: reference_%.c
	$(CC) -DPD_INTERNAL -O2 -ffunction-sections -fdata-sections -I$(PD_DIR)/src -DPD_BOB_SOURCE='"$(abspath $(PD_DIR))/extra/bob~/bob~.c"' -DPD_FILTER_SOURCE='"$(abspath $(PD_DIR))/src/d_filter.c"' -DPD_DELAY_SOURCE='"$(abspath $(PD_DIR))/src/d_delay.c"' -DPD_OSC_SOURCE='"$(abspath $(PD_DIR))/src/d_osc.c"' -c $< -o $@
reference_compare: reference_compare.cpp reference_bob.o reference_filters.o reference_osc.o reference_delay.o
	$(CXX) $(CXXFLAGS) $^ -Wl,--gc-sections -lm -o $@
check: engine_test
	./engine_test
clean:
	rm -f engine_test plugin_test adapter_test render reference_compare *.o
.PHONY: all check clean
adapter_test: adapter_test.cpp ../src/Vastish.cpp ../src/engine/*.hpp
	$(CXX) $(CXXFLAGS) -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include $< -L$(RACK_DIR) -Wl,-rpath,$(abspath $(RACK_DIR)) -lRack -o $@
