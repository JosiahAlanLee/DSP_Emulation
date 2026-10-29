RACK_DIR ?= ../../../deps/Rack-SDK
SOURCES += src/plugin.cpp src/Register.cpp src/Lookup.cpp src/Converter.cpp src/Counter.cpp src/Switch.cpp src/Pipeline.cpp src/Lookup2.cpp src/Counter2.cpp src/Fan.cpp src/Lattice.cpp src/ParallelSum.cpp src/Wheel.cpp src/Roulette.cpp src/Adapter.cpp src/Namagiri.cpp
DISTRIBUTABLES += res README.md LICENSE docs examples
EXTRA_CXXFLAGS += -std=c++17
EXTRA_FLAGS += -fno-unsafe-math-optimizations
include $(RACK_DIR)/arch.mk
ifdef ARCH_WIN
EXTRA_LDFLAGS += -static-libgcc -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic
endif
include $(RACK_DIR)/plugin.mk
