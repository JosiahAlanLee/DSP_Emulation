// SPDX-License-Identifier: BSD-3-Clause
// A frozen, memoryless Vastish chain evaluated at every possible byte address.
#pragma once
#include "Lookup.hpp"
#include "../../../vastish/src/engine/Engine.hpp"

namespace dspemu::lookup {
inline bool memoryless(int type) { return type == 0 || type == 7 || type == 8 || type == 11 || type == 12; }
struct FrozenControls {
    float note = 60, velocity = 1;
    std::array<float, vastish::SOURCE_COUNT> sources{};
};
inline Table compileVastish(vastish::Parameters params, const FrozenControls& frozen = {}) {
    if (!std::isfinite(frozen.note) || !std::isfinite(frozen.velocity)) throw std::runtime_error("Non-finite frozen note/velocity");
    if (frozen.note < -20 || frozen.note > 135 || frozen.velocity < 0 || frozen.velocity > 1)
        throw std::runtime_error("Frozen note/velocity outside Vastish input range");
    for (float s : frozen.sources) if (!std::isfinite(s)) throw std::runtime_error("Non-finite frozen source");
    for (int i = 0; i < vastish::PARAM_COUNT; ++i) {
        const auto& m = vastish::metadata()[i];
        if (!std::isfinite(params[i]) || params[i] < m.lo || params[i] > m.hi)
            throw std::runtime_error(std::string("Parameter outside Vastish metadata range: ") + m.name);
    }
    if (vastish::choice(params[vastish::PITCH_WAVE], vastish::AUDIO_WAVE) == vastish::AUDIO_WAVE)
        throw std::runtime_error("PITCH wave AUDIO processes live input and cannot be compiled to a table; choose another wave");
    for (int i = 0; i < 3; ++i) {
        int type = vastish::choice(params[vastish::F1_TYPE + 2 * i], 14);
        if (!memoryless(type)) throw std::runtime_error("Only NONE, SHAPER, DIST, GAIN and CRUSH can be compiled; stateful block in F" + std::to_string(i + 1));
    }
    vastish::Voice voice;
    voice.velocity = frozen.velocity; voice.sources = frozen.sources;
    voice.sources[0] = 0; voice.sources[8] = frozen.velocity; voice.sources[9] = frozen.note / 127.f;
    voice.sources[vastish::SRC_ONE] = 1;
    unsigned busy = 0, done = 0;
    for (int i = 0; i < vastish::FUN_COUNT; ++i) voice.evaluateFun(i, params, busy, done);
    const float* r = vastish::routes[std::max(0, vastish::choice(params[vastish::ALG], 6) - 1)];
    double rotation = (voice.control(vastish::PITCH_ADJ, params, frozen.note) - 60.) / 12.;
    Table table{};
    for (int n = 0; n < 256; ++n) {
        float x = float(wrap(bus::bipolar(uint8_t(n)) + 1. + 2 * wrap(rotation, 1.), 2.) - 1.);
        float a = voice.block[0].tick(vastish::choice(params[vastish::F1_TYPE], 14), x, voice.control(vastish::F1_ADJ, params, frozen.note), params[vastish::F1_B], 48000);
        float b = voice.block[1].tick(vastish::choice(params[vastish::F2_TYPE], 14), r[0]*x + r[1]*a, voice.control(vastish::F2_ADJ, params, frozen.note), params[vastish::F2_B], 48000);
        float c = voice.block[2].tick(vastish::choice(params[vastish::F3_TYPE], 14), r[2]*x + r[3]*a + r[4]*b + r[5]*a*b, voice.control(vastish::F3_ADJ, params, frozen.note), params[vastish::F3_B], 48000);
        float value = r[6]*a + r[7]*b + r[8]*c;
        if (!std::isfinite(value)) throw std::runtime_error("Frozen chain produced non-finite output");
        table[n] = bus::fromBipolar(value);
    }
    return table;
}
} // namespace dspemu::lookup
