CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fno-unsafe-math-optimizations
BACKEND = compile_vastish_backend$(if $(filter Windows_NT,$(OS)),.exe,)
all: $(BACKEND)
$(BACKEND): compile_vastish.cpp ../src/engine/CompileVastish.hpp ../src/engine/Lookup.hpp ../src/engine/LibraryIdentity.hpp ../../vastish/src/engine/*.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
clean:
	rm -f compile_vastish_backend compile_vastish_backend.exe
.PHONY: all clean
