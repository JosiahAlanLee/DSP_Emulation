// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
Plugin* pluginInstance;
void init(Plugin* plugin) {
    pluginInstance = plugin;
    plugin->addModel(modelDacStage);
    plugin->addModel(modelCzish);
}
