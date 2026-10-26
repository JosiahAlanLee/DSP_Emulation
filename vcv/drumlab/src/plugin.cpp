// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
Plugin* pluginInstance;
void init(Plugin* p) { pluginInstance = p; p->addModel(modelPercussion); }
