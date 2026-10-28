// SPDX-License-Identifier: AGPL-3.0-only
// The Monomachine's instance of the shared adapter (vcv/adapter): six tracks; the stereo mix, then
// six track lefts and six track rights; 16-frame native blocks, as Monomodule's DSP harness runs; and
// the hardware's two audio inputs, INP A and INP B, for the FX machines.
#pragma once
#include "../../../adapter/src/Scheduling.hpp"

namespace mnm_rack {
using Layout=emu_rack::Layout<6,14,16,2>;
constexpr int tracks=Layout::tracks, channels=Layout::channels, nativeFrames=Layout::nativeFrames, inputs=Layout::inputs;
constexpr int trackLeft=2, trackRight=2+tracks;   // first channel of each per-track stereo half
using emu_rack::params;
using emu_rack::frames;
using emu_rack::maxEvents;
using emu_rack::queueLatency;
using emu_rack::finite;
using emu_rack::Edge;
using emu_rack::Hit;
using emu_rack::Event;
using InputBlock=emu_rack::InputBlockOf<inputs>;
// An FX track's input, as the hardware's KIT > ROUTING names them, in a block's per-track settings.
enum FxSource : uint8_t { NEIBOR=0, INP_A=1, INP_B=2, INP_AB=3 };
constexpr FxSource defaultFxSource(int track) {return track?NEIBOR:INP_AB;}
using emu_rack::LinkState;
using emu_rack::resolve;
using OutputBlock=emu_rack::OutputBlock<channels>;
template<class T,size_t N> using Queue=emu_rack::Queue<T,N>;
inline Hit capture(int track, const gorgon_sound::Sound& sound, int selectedMachine,
                   float velocity, const float* knobs, const float* cv, float bpm, int note) {
    return emu_rack::capture<Layout>(track,sound,selectedMachine,velocity,knobs,cv,bpm,note);
}
// The note a V/OCT voltage plays: C4 = 0 V, as Gorgon's sound link and Rack's convention have it.
inline int noteOf(float volts) {return int(std::lround(60+12*finite(volts,-5,5.6f)));}
} // namespace mnm_rack
