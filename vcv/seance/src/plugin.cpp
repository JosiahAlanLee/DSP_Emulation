#include "plugin.hpp"
Plugin* pluginInstance;
void init(Plugin* p) {
    pluginInstance = p;
    p->addModel(modelSeance);
    p->addModel(modelSeanceFreeze);
    p->addModel(modelSeanceScramble);
    p->addModel(modelSeanceCross);
    p->addModel(modelSeanceWipe);
    p->addModel(modelSeanceBridge);
}
