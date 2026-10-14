RACK_DIR ?= ../deps/Rack-SDK
EXTRA_LDFLAGS += -pthread
SOURCES += src/plugin.cpp src/OtisSampler.cpp
DISTRIBUTABLES += res examples README.md LICENSE THIRD_PARTY_NOTICES.md docs
EXTRA_CXXFLAGS += -std=c++17 -pthread
EXTRA_FLAGS += -fno-unsafe-math-optimizations
include $(RACK_DIR)/plugin.mk
