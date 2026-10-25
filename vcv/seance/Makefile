RACK_DIR ?= ../../../deps/Rack-SDK
SOURCES += src/plugin.cpp src/Seance.cpp src/Expanders.cpp src/Bridge.cpp
DISTRIBUTABLES += res README.md LICENSE docs examples
EXTRA_CXXFLAGS += -std=c++17
# Reconstruction and the seeded textures rely on IEEE semantics.
EXTRA_FLAGS += -fno-unsafe-math-optimizations
include $(RACK_DIR)/arch.mk
ifdef ARCH_WIN
	# MinGW runtimes linked statically, as Rack does not ship them.
	EXTRA_LDFLAGS += -static-libgcc -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic
endif
include $(RACK_DIR)/plugin.mk
