// SPDX-License-Identifier: AGPL-3.0-only
// The shared worker (vcv/adapter) for the Monomachine's layout.
#pragma once
#include "Renderer.hpp"
#include "ShortGates.hpp"
#include "../../../adapter/src/Worker.hpp"

namespace mnm_rack {
// Every backend runs inside ShortGates, so a gate shorter than one engine block still sounds.
class Worker:public emu_rack::Worker<Layout> {
    static Factory wrap(Factory make) {
        return [make=std::move(make)](const std::string& path,const std::string& samples)->std::unique_ptr<Backend>{
            return std::make_unique<ShortGates>(make(path,samples));};
    }
public:
    explicit Worker(Factory make):emu_rack::Worker<Layout>(wrap(std::move(make)),"Load the Monomachine OS 1.32B .syx from the module menu"){}
};
} // namespace mnm_rack
