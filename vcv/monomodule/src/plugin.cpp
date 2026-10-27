// SPDX-License-Identifier: AGPL-3.0-only
#include "plugin.hpp"
Plugin* pluginInstance;
void init(Plugin* p){pluginInstance=p;p->addModel(modelMonomodule);}
