CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fno-unsafe-math-optimizations
RACK_DIR ?= ../../../../deps/Rack-SDK
SDK_FLAGS = -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include -L$(RACK_DIR) -Wl,-rpath,$(abspath $(RACK_DIR)) -lRack -Wl,--allow-shlib-undefined
HEADERS = ../src/engine/Register.hpp ../src/engine/Counter.hpp ../src/engine/Switch.hpp ../src/engine/Pipeline.hpp ../src/engine/Counter2.hpp ../src/engine/Fan.hpp ../src/engine/Lattice.hpp ../src/engine/ParallelSum.hpp ../src/engine/Wheel.hpp ../src/engine/Roulette.hpp ../src/engine/ClockOwner.hpp ../../shared/ByteBus.hpp
LOOKUP_HEADERS = ../src/engine/Lookup.hpp ../src/engine/LibraryIdentity.hpp ../src/engine/CompileVastish.hpp ../src/engine/Lookup2.hpp
UI_HEADERS = ../src/Layout.hpp ../../shared/PanelWidgets.hpp
ADAPTER_HEADERS = ../src/engine/Adapter.hpp ../src/engine/ClockOwner.hpp ../../shared/ByteBus.hpp ../../shared/DacStage.hpp ../../shared/RateConverter.hpp
all: engine_test module_test lookup_test lookup_module_test io_test io_module_test adapter_test adapter_module_test
adapter_test: adapter_test.cpp $(ADAPTER_HEADERS)
	$(CXX) $(CXXFLAGS) -Wno-mismatched-new-delete $< -o $@
adapter_module_test: adapter_module_test.cpp ../src/Adapter.cpp ../src/Converter.cpp ../src/Register.cpp ../src/engine/Register.hpp ../examples/adapter-cz-audio.vcv ../examples/adapter-repeated-triggers.vcv $(ADAPTER_HEADERS) $(UI_HEADERS) ../../czish/src/*.cpp ../../czish/src/engine/*.hpp ../../vastish/src/Vastish.cpp ../../vastish/src/engine/*.hpp ../../otislab/src/OtisSampler.cpp ../../otislab/src/engine/*.hpp ../../shared/*.hpp ../../seance/src/engine/*.hpp
	$(CXX) $(CXXFLAGS) -pthread $< $(SDK_FLAGS) -o $@
io_module_test: io_module_test.cpp ../src/Converter.cpp ../src/engine/Converter.hpp ../src/Register.cpp ../src/Lookup.cpp ../../vastish/src/Vastish.cpp ../../vastish/src/engine/*.hpp ../../otislab/src/OtisSampler.cpp ../../otislab/src/engine/*.hpp ../../shared/*.hpp $(UI_HEADERS)
	$(CXX) $(CXXFLAGS) -pthread $< $(SDK_FLAGS) -o $@
io_test: io_test.cpp ../src/engine/Converter.hpp ../../vastish/src/engine/*.hpp ../../shared/ByteBus.hpp
	$(CXX) $(CXXFLAGS) -Wno-mismatched-new-delete $< -o $@
engine_test: engine_test.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@
module_test: module_test.cpp ../src/Register.cpp ../src/Counter.cpp ../src/Fan.cpp ../src/Lattice.cpp ../src/ParallelSum.cpp ../src/Wheel.cpp ../src/Roulette.cpp ../src/Switch.cpp ../src/Pipeline.cpp ../src/Lookup2.cpp ../src/Counter2.cpp ../src/engine/Counter.hpp ../src/Lookup.cpp $(LOOKUP_HEADERS) $(HEADERS) ../../vastish/src/Vastish.cpp ../../vastish/src/engine/*.hpp $(UI_HEADERS)
	$(CXX) $(CXXFLAGS) $< $(SDK_FLAGS) -o $@
lookup_test: lookup_test.cpp $(LOOKUP_HEADERS) ../../vastish/src/engine/*.hpp
	$(CXX) $(CXXFLAGS) -Wno-mismatched-new-delete $< -o $@
lookup_module_test: lookup_module_test.cpp ../src/Register.cpp ../src/Lookup.cpp ../src/Lookup2.cpp $(HEADERS) $(LOOKUP_HEADERS) $(UI_HEADERS)
	$(CXX) $(CXXFLAGS) $< $(SDK_FLAGS) -o $@
test: engine_test lookup_test io_test adapter_test
	./engine_test
	./lookup_test
	./io_test
	./adapter_test
check: all
	./engine_test
	./module_test
	./lookup_test
	./lookup_module_test
	./io_test
	./io_module_test
	./adapter_test
	./adapter_module_test
sanitize:
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer adapter_test.cpp -o adapter_asan
	./adapter_asan
	$(CXX) -std=c++17 -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer adapter_module_test.cpp $(SDK_FLAGS) -o adapter_module_asan
	ASAN_OPTIONS=detect_leaks=0 ./adapter_module_asan
	$(CXX) -std=c++17 -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer io_module_test.cpp $(SDK_FLAGS) -o io_module_asan
	ASAN_OPTIONS=detect_leaks=0 ./io_module_asan
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer io_test.cpp -o io_asan
	ASAN_OPTIONS=detect_leaks=0 ./io_asan
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer engine_test.cpp -o engine_asan
	./engine_asan
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer module_test.cpp $(SDK_FLAGS) -o module_asan
	ASAN_OPTIONS=detect_leaks=0 ./module_asan
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer lookup_test.cpp -o lookup_asan
	ASAN_OPTIONS=detect_leaks=0 ./lookup_asan
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer lookup_module_test.cpp $(SDK_FLAGS) -o lookup_module_asan
	ASAN_OPTIONS=detect_leaks=0 ./lookup_module_asan
clean:
	rm -f engine_test module_test lookup_test lookup_module_test io_test io_module_test io_asan io_module_asan engine_asan module_asan lookup_asan lookup_module_asan
	rm -f adapter_test adapter_module_test adapter_asan adapter_module_asan
tools-check:
	$(MAKE) -C ../tools
	python3 -m unittest -v test_lookup_tools.py
.PHONY: all test check sanitize clean tools-check
