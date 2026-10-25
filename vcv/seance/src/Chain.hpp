// SPDX-License-Identifier: BSD-3-Clause
// How a row of Séance modules finds its frame. The core publishes a fixed pointer to its
// FrameV1 on its own rightExpander slots (never flipped, like the sample-share link in
// vcv/shared). Each expander walks left through its neighbours every step until it reaches
// the core, counting its position on the way; it touches only Module pointers that Rack
// keeps valid for the step, never a cached pointer that a removed core could have left
// behind. The frame itself is passed with the token in FrameV1::stage (engine/Spectral.hpp),
// which is safe under Rack's worker threads whatever order the modules are stepped in.
#pragma once
#include "plugin.hpp"
#include "engine/Spectral.hpp"

namespace seance {

inline bool isExpander(const Model* m) {
    return m == modelSeanceFreeze || m == modelSeanceScramble || m == modelSeanceCross || m == modelSeanceWipe || m == modelSeanceBridge;
}

// From an expander: the frame of the core at the head of its row, and this module's
// position in the row (1 = next to the core). Null and 0 when the row has no core.
inline FrameV1* findFrame(Module* left, int& position) {
    position = 0;
    for (int hops = 1; left && hops < 64; hops++) {
        if (left->model == modelSeance) {
            void* p = left->rightExpander.producerMessage;
            if (!validFrame(p)) return nullptr;
            position = hops;
            return static_cast<FrameV1*>(p);
        }
        if (!isExpander(left->model)) return nullptr;
        left = left->leftExpander.module;
    }
    return nullptr;
}

// Common part of the four expanders: find the frame each step and serve it when it is
// here. Bypassed modules still pass it on, untouched.
struct Expander : Module {
    std::atomic<int> shownPosition{0};  // for the LINK light and the menu

    template <class Op>
    void run(bool bypassed, Op&& op) {
        int position;
        FrameV1* frame = findFrame(leftExpander.module, position);
        shownPosition.store(frame ? position : 0, std::memory_order_relaxed);
        if (!frame) return;
        bool last = !(rightExpander.module && isExpander(rightExpander.module->model));
        serve(*frame, position, last, bypassed, op);
    }
    void processBypass(const ProcessArgs& args) override {
        run(true, [](FrameV1&) {});
        Module::processBypass(args);
    }
    void linkMenu(Menu* menu) {
        int pos = shownPosition.load();
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel(pos ? string::f("Linked: position %d in the row", pos) : "Not linked: place a Séance core, or another expander, directly to the left"));
    }
};

} // namespace seance
