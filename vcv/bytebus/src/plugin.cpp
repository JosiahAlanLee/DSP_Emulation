#include "plugin.hpp"
Plugin* pluginInstance;
void init(Plugin* plugin) {
    pluginInstance = plugin;
    plugin->addModel(modelClockedRegister);
    plugin->addModel(modelLookup);
    plugin->addModel(modelConverter);
    plugin->addModel(modelCounter);
    plugin->addModel(modelSwitch);
    plugin->addModel(modelPipeline);
    plugin->addModel(modelLookup2);
    plugin->addModel(modelCounter2);
    plugin->addModel(modelFan);
    plugin->addModel(modelLattice);
}
