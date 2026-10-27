// SPDX-License-Identifier: AGPL-3.0-only
// The Machinedrum's instance of the shared adapter (vcv/adapter): sixteen tracks; the stereo mix and
// sixteen track outputs; 32-frame native blocks.
#pragma once
#include "../../../adapter/src/Scheduling.hpp"

namespace md_rack {
using Layout=emu_rack::Layout<16,18>;
constexpr int tracks=Layout::tracks, channels=Layout::channels;
using emu_rack::params;
using emu_rack::frames;
using emu_rack::maxEvents;
using emu_rack::queueLatency;
using emu_rack::finite;
using emu_rack::Edge;
using emu_rack::Hit;
using emu_rack::Event;
using emu_rack::InputBlock;
using emu_rack::LinkState;
using emu_rack::resolve;
using OutputBlock=emu_rack::OutputBlock<channels>;
template<class T,size_t N> using Queue=emu_rack::Queue<T,N>;
inline Hit capture(int track, const gorgon_sound::Sound& sound, int selectedMachine,
                   float velocity, const float* knobs, const float* cv, float bpm) {
    return emu_rack::capture<Layout>(track,sound,selectedMachine,velocity,knobs,cv,bpm);
}
} // namespace md_rack
