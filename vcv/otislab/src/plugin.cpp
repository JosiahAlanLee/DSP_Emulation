#include "plugin.hpp"
Plugin* pluginInstance;
void init(Plugin* p) {
    pluginInstance=p;
    p->addModel(modelOtisSampler);
    p->addModel(modelOtisKit);
    p->addModel(modelOtisCapture);
}
