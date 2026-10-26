RACK_DIR ?= ../../../deps/Rack-SDK
SOURCES += src/plugin.cpp src/Percussion.cpp
DISTRIBUTABLES += res README.md LICENSE docs examples
EXTRA_CXXFLAGS += -std=c++17
EXTRA_FLAGS += -fno-unsafe-math-optimizations
include $(RACK_DIR)/arch.mk
ifdef ARCH_WIN
EXTRA_LDFLAGS += -static-libgcc -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic
endif
include $(RACK_DIR)/plugin.mk
