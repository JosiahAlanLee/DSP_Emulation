// SPDX-License-Identifier: BSD-3-Clause
// Sample-timed calibration input; deliberately not a general MIDI player.
#pragma once
#include "../src/engine/CzVoice.hpp"
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace calibration {
struct Event {
    uint64_t frame;
    int slot, note, gate;
};
inline int renderSequence(const dspemu::cz::Tone &tone, const std::string &schedule,
                          const std::string &destination, const std::string &model, bool clean) {
    if (model != "native" && model != "instant" && model != "syllabic")
        throw std::runtime_error("Unknown model: use native, instant or syllabic");
    std::ifstream input(schedule);
    std::string magic;
    int rate = 0;
    uint64_t frames = 0;
    if (!(input >> magic >> rate >> frames) || magic != "CZSEQ1" || rate < 8000 || rate > 192000 || !frames ||
        frames > uint64_t(rate) * 300 || (model == "native" && rate != 40000))
        throw std::runtime_error("Invalid CZSEQ1 header (native requires 40000 Hz)");
    std::vector<Event> events;
    std::array<bool, 8> active{};
    std::array<int, 8> notes{};
    int channels = 1;
    while (true) {
        input >> std::ws;
        if (input.peek() == std::char_traits<char>::eof())
            break;
        Event e{};
        if (!(input >> e.frame >> e.slot >> e.note >> e.gate) || e.frame >= frames || e.slot < 0 ||
            e.slot >= dspemu::cz::noteLimit(tone) || e.note < 0 || e.note > 127 ||
            (e.gate != 0 && e.gate != 1) || (!events.empty() && e.frame < events.back().frame))
            throw std::runtime_error("Invalid or unsorted CZSEQ1 event");
        if (active[e.slot] == bool(e.gate) || (!e.gate && notes[e.slot] != e.note))
            throw std::runtime_error("Unmatched or overlapping CZSEQ1 note");
        for (auto it = events.rbegin(); it != events.rend() && it->frame == e.frame; ++it)
            if (it->slot == e.slot)
                throw std::runtime_error("A retrigger needs at least one low gate sample");
        active[e.slot] = e.gate;
        notes[e.slot] = e.note;
        channels = std::max(channels, e.slot + 1);
        events.push_back(e);
    }
    if (std::any_of(active.begin(), active.end(), [](bool x) { return x; }))
        throw std::runtime_error("CZSEQ1 has a missing note-off");
    std::ofstream out(destination, std::ios::binary);
    if (!out)
        throw std::runtime_error("Cannot open WAV output");
    auto le = [&](uint32_t v, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out.put(char(v >> (8 * i)));
    };
    // IEEE float keeps low-level converter differences and avoids audition clipping.
    out.write("RIFF", 4);
    le(50 + frames * 4, 4);
    out.write("WAVEfmt ", 8);
    le(18, 4);
    le(3, 2);
    le(1, 2);
    le(rate, 4);
    le(rate * 4, 4);
    le(4, 2);
    le(32, 2);
    le(0, 2);
    out.write("fact", 4);
    le(4, 4);
    le(frames, 4);
    out.write("data", 4);
    le(frames * 4, 4);
    auto engine = std::make_unique<dspemu::cz::VoiceEngine>();
    dspemu::cz::RenderSettings settings;
    settings.polyEnabled = false;
    settings.czModel = model == "syllabic" ? dspemu::dac::CzModel::Syllabic : dspemu::dac::CzModel::Instant;
    std::array<dspemu::cz::Input, 8> in{};
    size_t next = 0;
    for (uint64_t frame = 0; frame < frames; ++frame) {
        while (next < events.size() && events[next].frame == frame) {
            const auto &e = events[next++];
            in[e.slot].pitch = (e.note - 60) / 12.f;
            in[e.slot].gate = e.gate ? 10.f : 0.f;
        }
        float sample = engine->process(in, channels, tone, 0, rate, clean, settings);
        if (model == "native") {
            int sum = 0;
            for (auto voice : engine->chip.lastVoices)
                sum += voice;
            sample = float(sum) / 32768.f;
        }
        uint32_t bits;
        static_assert(sizeof(sample) == sizeof(bits));
        std::memcpy(&bits, &sample, sizeof(bits));
        le(bits, 4);
    }
    if (!out)
        throw std::runtime_error("Could not finish WAV output");
    return 0;
}
} // namespace calibration
