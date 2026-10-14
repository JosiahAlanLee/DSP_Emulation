RACK_DIR ?= ../deps/Rack-SDK
EXTRA_LDFLAGS += -pthread
SOURCES += src/plugin.cpp src/OtisSampler.cpp
DISTRIBUTABLES += res examples README.md LICENSE THIRD_PARTY_NOTICES.md docs
EXTRA_CXXFLAGS += -std=c++17 -pthread
EXTRA_FLAGS += -fno-unsafe-math-optimizations
# Windows: link the MinGW runtimes statically. Rack does not ship libgcc_s_seh-1.dll
# or libwinpthread-1.dll, so a plugin.dll that imports them fails to load.
include $(RACK_DIR)/arch.mk
ifdef ARCH_WIN
	EXTRA_LDFLAGS += -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
endif
include $(RACK_DIR)/plugin.mk
