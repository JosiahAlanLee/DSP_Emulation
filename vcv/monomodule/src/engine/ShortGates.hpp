// SPDX-License-Identifier: AGPL-3.0-only
// Gates shorter than one engine block. The shared renderer hands a block's events to the backend before
// it renders that block, and the Monomachine's host keeps one note byte per block: a note-on then a
// note-off leaves the off (core HostModel.cpp), so a gate whose rise and fall land in one 16-frame block
// would never sound. This wraps any backend and holds such a release until the block that started the
// note has rendered, so the note plays for one block (16 engine samples, 0.36 ms) and then ends.
// Machinemodule, on the same renderer, is unaffected: its drum hits have no note-off.
#pragma once
#include "Renderer.hpp"
#include <memory>

namespace mnm_rack {
class ShortGates final:public Backend {
    std::unique_ptr<Backend> inner;
    std::array<bool,tracks> started{}, held{};   // a note began in this block; its release waits for the next
public:
    explicit ShortGates(std::unique_ptr<Backend> backend):inner(std::move(backend)){}
    std::string description() const override {return inner->description();}
    std::vector<Machine> machines() const override {return inner->machines();}
    bool fire(const Hit& hit) override {
        bool played=inner->fire(hit);
        // A later note in the same block cancels a held release: the off is moot, the on retrigs.
        if(played){started[hit.track]=true;held[hit.track]=false;}
        return played;
    }
    void release(int track) override {
        if(started[size_t(track)])held[size_t(track)]=true;
        else inner->release(track);
    }
    void configure(const uint8_t (&settings)[emu_rack::trackSettings]) override {inner->configure(settings);}
    void input(const float* frames) override {inner->input(frames);}
    void panic() override {started.fill(false);held.fill(false);inner->panic();}
    void render(float (&out)[nativeFrames][channels]) override {
        inner->render(out);
        // Before the next block's events, so a note-on there still follows this release.
        for(int t=0;t<tracks;++t)if(held[size_t(t)])inner->release(t);
        started.fill(false);held.fill(false);
    }
};
} // namespace mnm_rack
