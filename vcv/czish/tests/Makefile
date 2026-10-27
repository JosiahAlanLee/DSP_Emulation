CXX ?= g++
DEPS = $(wildcard ../src/engine/*.hpp ../../shared/*.hpp)
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fno-unsafe-math-optimizations
RACK_DIR ?= ../../../../deps/Rack-SDK
SDK_FLAGS = -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include -L$(RACK_DIR) -Wl,-rpath,$(abspath $(RACK_DIR)) -lRack -Wl,--allow-shlib-undefined

mame_ref_generated.hpp: make_mame_reference.py reference/mame_upd933.cpp
	python3 make_mame_reference.py

core_test: $(DEPS) core_test.cpp mame_ref_generated.hpp reference/ymfm.h ../src/engine/Upd933.hpp ../../shared/DacStage.hpp
	$(CXX) $(CXXFLAGS) core_test.cpp -o $@

render: ../tools/render.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) $< -o $@

frontend_test: frontend_test.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) $< -o $@

module_test: $(DEPS) module_test.cpp ../src/DacStage.cpp ../src/Czish.cpp ../src/Layout.hpp ../src/engine/CzVoice.hpp ../src/engine/Upd933.hpp ../../shared/DacStage.hpp ../../shared/ByteBus.hpp ../../shared/PanelWidgets.hpp ../../bytebus/src/Register.cpp ../../bytebus/src/engine/Register.hpp
	$(CXX) $(CXXFLAGS) module_test.cpp $(SDK_FLAGS) -o $@

test: core_test frontend_test
	./core_test
	./frontend_test

check: core_test frontend_test module_test render
	./core_test
	./frontend_test
	./module_test
	python3 ../tools/make_example.py --check
	python3 ../tools/check_capture.py

sanitize:
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer frontend_test.cpp -o frontend_asan
	ASAN_OPTIONS=detect_leaks=0 ./frontend_asan
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer module_test.cpp $(SDK_FLAGS) -o module_asan
	ASAN_OPTIONS=detect_leaks=0 ./module_asan

clean:
	rm -f core_test module_test frontend_test render module_asan frontend_asan mame_ref_generated.hpp

.PHONY: test check sanitize clean
