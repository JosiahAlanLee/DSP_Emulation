// SPDX-License-Identifier: AGPL-3.0-only
// The shared worker (vcv/adapter) for the Monomachine's layout.
#pragma once
#include "Renderer.hpp"
#include "../../../adapter/src/Worker.hpp"

namespace mnm_rack {
class Worker:public emu_rack::Worker<Layout> {
public:
    explicit Worker(Factory make):emu_rack::Worker<Layout>(std::move(make),"Load the Monomachine OS 1.32B .syx from the module menu"){}
};
} // namespace mnm_rack
