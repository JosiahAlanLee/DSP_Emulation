// SPDX-License-Identifier: AGPL-3.0-only
// What the audio thread and the worker exchange, for any emulated instrument: hits, events, the
// blocks of them, the lock-free queue, Gorgon sound-link reception and per-hit control capture.
// An instrument instantiates it with its Layout (see vcv/adapter/README.md).
#pragma once
#include "../../shared/GorgonSound.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace emu_rack {
static_assert(std::atomic<uint64_t>::is_always_lock_free,"The Rack callback requires lock-free queue counters");
constexpr int params=8, frames=128, maxEvents=64;
constexpr int queueLatency=1024; // host samples, independent of Rack's block size
// An instrument's shape: its tracks, its output channels (the stereo mix, then one per track), and the
// frames the emulator renders at a time (its voice/control interface).
template<int Tracks,int Channels,int NativeFrames=32> struct Layout {
    static constexpr int tracks=Tracks, channels=Channels, nativeFrames=NativeFrames;
};
inline float finite(float v, float lo, float hi, float fallback=0) {
    return std::isfinite(v)?std::clamp(v,lo,hi):fallback;
}
struct Edge {
    bool high=false;
    bool process(float v) {
        bool before=high;
        if (!std::isfinite(v) || v<=.1f) high=false;
        else if (v>=1.f) high=true;
        return high&&!before;
    }
};
struct Hit {
    int16_t machine=16;
    uint16_t mask=0;
    float values[params]={}, offsets[params]={};
    float bpm=120;
    uint8_t track=0, velocity=127;
    int8_t note=-1;          // MIDI note for a pitched instrument; -1 for none (a drum machine ignores it)
};
// A hit, a panic, or a release: the gate of hit.track fell, so a pitched instrument ends its note.
struct Event { uint16_t offset=0; bool panic=false, release=false; Hit hit; };
struct InputBlock {
    uint64_t generation=0;
    int64_t start=0;
    double rate=48000;
    uint16_t count=0;
    bool overflow=false;
    Event events[maxEvents];
    bool append(const Event& event) {
        if (count==maxEvents) {overflow=true;return false;}
        events[count++]=event;return true;
    }
};
template<int Channels> struct OutputBlock {
    uint64_t generation=0;
    int64_t start=0;
    float samples[frames][Channels]={};
};
static_assert(std::is_trivially_copyable<InputBlock>::value,"Event queue must own whole immutable snapshots");
// Single producer / single consumer, never reset while either thread is alive.
// Generations invalidate old blocks without racing a queue-index reset.
template<class T, size_t N> class Queue {
    std::array<T,N> slots{};
    alignas(64) std::atomic<uint64_t> written{0};
    alignas(64) std::atomic<uint64_t> read{0};
public:
    bool push(const T& value) {
        auto w=written.load(std::memory_order_relaxed);
        if(w-read.load(std::memory_order_acquire)==N)return false;
        slots[w%N]=value;written.store(w+1,std::memory_order_release);return true;
    }
    const T* front() const {
        auto r=read.load(std::memory_order_relaxed);
        if(r==written.load(std::memory_order_acquire))return nullptr;
        return &slots[r%N];
    }
    void pop() {read.fetch_add(1,std::memory_order_release);}
};
template<class L>
inline Hit capture(int track, const gorgon_sound::Sound& sound, int selectedMachine,
                   float velocity, const float* knobs, const float* cv, float bpm, int note=-1) {
    Hit hit;
    hit.note=int8_t(note<0?-1:std::clamp(note,0,127));
    hit.track=uint8_t(std::clamp(track,0,L::tracks-1));
    hit.machine=int16_t(sound.machine>=0?sound.machine:std::clamp(selectedMachine,0,191));
    hit.mask=sound.mask&255;
    hit.velocity=uint8_t(std::lround(finite(velocity,0,1)*127));
    hit.bpm=finite(bpm,30,300,120);
    for(int i=0;i<params;++i){
        hit.values[i]=finite(sound.values[i],0,127);
        hit.offsets[i]=finite(knobs[i],-127,127)+finite(cv[i],-100,100)*12.7f;
    }
    return hit;
}
inline std::array<int,params> resolve(const Hit& hit, const std::array<uint8_t,params>& defaults) {
    std::array<int,params> result;
    for(int i=0;i<params;++i)
        result[i]=int(std::lround(finite(((hit.mask&(1u<<i))?hit.values[i]:defaults[i])+hit.offsets[i],0,127)));
    return result;
}
// The Rack receiver owns the expander buffers. Only consume live, fresh packets.
struct LinkState {
    bool connected=false, moving=false;
    int64_t transport=-1, lastFrame=-1;
    std::array<Edge,gorgon_sound::LANES> gates;
    Edge reset;
    // Results are per-sample; the caller copies complete events into its block. `releases` are lanes
    // whose gate fell, for a pitched instrument's note-offs; a drum machine ignores them.
    // `resetOnly`: the panic is a reset pulse alone (a loop wrap), with the transport unchanged and moving.
    struct Result { bool panic=false, resetOnly=false; std::array<bool,gorgon_sound::LANES> hits{}, releases{}; };
    Result update(const gorgon_sound::Packet* packet, int64_t frame) {
        Result result;
        if(!packet||!gorgon_sound::valid(*packet,frame)){
            result.panic=connected&&moving;connected=moving=false;transport=-1;lastFrame=-1;
            for(auto& e:gates)e.high=false;reset.high=false;
            return result;
        }
        if(!connected){ // Connecting in the middle of a pulse must not invent a hit.
            connected=true;moving=packet->moving;transport=packet->transport;lastFrame=packet->frame;
            for(int i=0;i<gorgon_sound::LANES;++i)gates[i].high=packet->lanes[i].gate>.1f;
            reset.high=packet->reset>.1f;return result;
        }
        if(packet->frame<=lastFrame)return result; // each producer frame is consumed once
        lastFrame=packet->frame;
        bool jumped=packet->transport!=transport;
        bool resetEdge=reset.process(packet->reset);
        result.panic=(moving&&!packet->moving)||jumped||resetEdge;
        result.resetOnly=resetEdge&&!jumped&&!(moving&&!packet->moving);
        // A seek/run revision starts a fresh trigger phase. A repeated reset pulse does not.
        if(jumped)for(auto& e:gates)e.high=false;
        for(int i=0;i<gorgon_sound::LANES;++i){
            bool was=gates[i].high,edge=gates[i].process(packet->lanes[i].gate);
            result.hits[i]=packet->moving&&edge&&packet->lanes[i].velocity>0;
            result.releases[i]=was&&!gates[i].high;
        }
        moving=packet->moving;transport=packet->transport;
        return result;
    }
};
} // namespace emu_rack
