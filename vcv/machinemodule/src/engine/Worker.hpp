// SPDX-License-Identifier: AGPL-3.0-only
// The shared worker (vcv/adapter) for the Machinedrum's layout.
#pragma once
#include "Renderer.hpp"
#include "../../../adapter/src/Worker.hpp"

namespace md_rack {
class Worker:public emu_rack::Worker<Layout> {
public:
    explicit Worker(Factory make):emu_rack::Worker<Layout>(std::move(make),"Load a Machinedrum OS .syx from the module menu"){}
};
} // namespace md_rack
