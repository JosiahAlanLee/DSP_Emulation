// SPDX-License-Identifier: BSD-3-Clause
// Explicit codecs and a clock-owning byte adapter. No Rack types or allocation.
#pragma once
#include "ClockOwner.hpp"
#include "../../../shared/DacStage.hpp"
#include <array>

namespace dspemu::adapter {
enum class Format { Offset, Signed, Casio, Yamaha };
enum class Clock { Follow, Jack, Internal, Engine };
enum class Preset { Relay, CasioAudio, YamahaAudio, LinearAudio, ToCasio, ToYamaha,
    GorgonHits, GorgonMutes, Slow, CenteredMask, Count };

inline int16_t decode(uint8_t word, Format format, float tracking = 0.f) {
    switch (format) {
        case Format::Signed: return int16_t((word < 128 ? int(word) : int(word)-256)*256);
        case Format::Casio: return dac::casioDecode(uint16_t(word) << 6, tracking);
        case Format::Yamaha: return dac::yamahaDecode(uint16_t(word) << 5);
        default: return int16_t((int(word)-128)*256);
    }
}
inline uint8_t encode(int16_t pcm, Format format) {
    switch (format) {
        case Format::Signed: return uint8_t(dac::linearEncode(pcm, 8));
        case Format::Casio: return uint8_t(dac::casioEncode(pcm) >> 6);
        case Format::Yamaha: return uint8_t(dac::yamahaEncode(pcm) >> 5);
        default: return bus::fromBipolar(float(pcm)/32768.f);
    }
}
inline uint8_t convert(uint8_t word, Format from, Format to, float tracking = 0.f) {
    return from == to ? word : encode(decode(word, from, tracking), to);
}
struct Parameters {
    Format input = Format::Offset, output = Format::Offset;
    Clock clock = Clock::Follow;
    double rateHz = 8;
    uint8_t keep = 255, flip = 0, idle = 0;
    float tracking = 0;
    std::array<int, 8> route{{0,1,2,3,4,5,6,7}}; // -1 = Off
};
inline Parameters preset(Preset choice) {
    Parameters p;
    switch (choice) {
        case Preset::CasioAudio: p.input=Format::Casio; p.idle=128; break;
        case Preset::YamahaAudio: p.input=Format::Yamaha; p.idle=128; break;
        case Preset::LinearAudio: p.input=Format::Signed; p.idle=128; break;
        case Preset::ToCasio: p.output=Format::Casio; break;
        case Preset::ToYamaha: p.output=Format::Yamaha; p.idle=0x30; break;
        case Preset::GorgonHits: p.keep=0x3f; break;
        case Preset::GorgonMutes: p.keep=0x7f; p.flip=0x3f; break;
        case Preset::Slow: p.clock=Clock::Internal; break;
        case Preset::CenteredMask: p.output=Format::Signed; break;
        default: break;
    }
    return p;
}
inline uint8_t transform(uint8_t word, const Parameters& p) {
    uint8_t value=convert(word,p.input,p.output,p.tracking), routed=0;
    for (int bit=0; bit<8; ++bit)
        if (p.route[bit]>=0 && p.route[bit]<8 && (value & (1u << p.route[bit]))) routed |= uint8_t(1u << bit);
    return uint8_t((routed & p.keep) ^ p.flip);
}
struct Output {
    uint8_t word=0, triggers=0;
    bool clocked=false, clk=false, acquired=false, loaded=false;
    float volts=0, level=0;
};
struct Engine {
    clock_owner::ClockOwner timing;
    uint8_t word=0, previousTriggers=0;
    bool initialized=false, connected=false, inputClocked=false, seedEngine=true;
    Clock previousClock=Clock::Follow;

    void clear(uint8_t idle=0) {
        timing.clear(); word=previousTriggers=idle;
        initialized=false; connected=inputClocked=false; seedEngine=true;
        previousClock=Clock::Follow;
    }
    Output tick(const Parameters& p, const clock_owner::Inputs& in, double sampleRate) {
        if (!initialized || connected!=in.data.connected || inputClocked!=in.data.clocked) {
            clear(p.idle); initialized=true;
            connected=in.data.connected; inputClocked=in.data.clocked;
        }
        if (p.clock!=previousClock) seedEngine=true;
        previousClock=p.clock;
        using M=clock_owner::ClockMode;
        M mode=p.clock==Clock::Jack ? M::Jack : p.clock==Clock::Internal ? M::Internal :
            p.clock==Clock::Engine || !in.data.clocked ? M::Engine : M::Bus;
        auto step=timing.tick(mode,p.rateHz,in,sampleRate);
        if (step.seed) { word=p.idle; seedEngine=true; }
        Output out;
        if (step.acquire) {
            word=in.data.connected ? transform(in.data.rawWord,p) : p.idle;
            if (in.data.connected) {
                out.acquired=true;
                out.triggers=step.clocked ? word : seedEngine ? 0 : uint8_t(word & ~previousTriggers);
            }
            previousTriggers=word; seedEngine=false;
        }
        if (step.loaded) { previousTriggers=word; seedEngine=true; }
        out.word=word; out.clocked=step.clocked; out.clk=step.clk; out.loaded=step.loaded;
        out.volts=bus::toVolts(word); out.level=10.f*bus::level(word);
        return out;
    }
};
} // namespace dspemu::adapter
