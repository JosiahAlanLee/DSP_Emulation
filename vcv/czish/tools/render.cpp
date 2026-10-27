// SPDX-License-Identifier: BSD-3-Clause
// Offline audition and owner-recording fixtures. No manufacturer tones/ROMs.
#include "../src/engine/CzVoice.hpp"
#include "../src/engine/Sysex.hpp"
#include "RenderSequence.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace dspemu;
static void le(std::ostream &f, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i)
        f.put(char(v >> (8 * i)));
}
static void be(std::ostream &f, uint32_t v, int bytes) {
    for (int i = bytes - 1; i >= 0; --i)
        f.put(char(v >> (8 * i)));
}
static void vlq(std::vector<uint8_t> &v, unsigned value) {
    uint8_t b[4];
    int n = 0;
    b[n++] = value & 127;
    while ((value >>= 7))
        b[n++] = uint8_t((value & 127) | 128);
    while (n)
        v.push_back(b[--n]);
}
static void midi(const std::filesystem::path &path, const std::vector<int> &notes, double seconds,
                 bool burst) {
    std::vector<uint8_t> v{0, 0xff, 0x51, 3, 7, 0xa1, 0x20};
    for (int round = 0; round < (burst ? 8 : 1); ++round) {
        for (auto note : notes) {
            vlq(v, 0);
            v.insert(v.end(), {0x90, uint8_t(note), 64});
        }
        for (size_t i = 0; i < notes.size(); ++i) {
            vlq(v, i == 0 ? unsigned((burst ? .01 : seconds) * 1920) : 0);
            v.insert(v.end(), {0x80, uint8_t(notes[i]), 0});
        }
        if (burst) {
            vlq(v, 960);
            v.insert(v.end(), {0xff, 1, 0});
        }
    }
    vlq(v, 1920);
    v.insert(v.end(), {0xff, 0x2f, 0});
    std::ofstream f(path, std::ios::binary);
    f.write("MThd", 4);
    be(f, 6, 4);
    be(f, 0, 2);
    be(f, 1, 2);
    be(f, 960, 2);
    f.write("MTrk", 4);
    be(f, v.size(), 4);
    f.write(reinterpret_cast<const char *>(v.data()), v.size());
}
static void captures(const std::filesystem::path &dir) {
    std::filesystem::create_directories(dir);
    std::ofstream manifest(dir / "manifest.tsv", std::ios::binary);
    manifest << "name\tnotes\theld_seconds\tdescription\n";
    auto save = [&](std::string name, cz::Tone tone, std::vector<int> notes, double sec,
                    std::string description, bool burst = false) {
        auto bytes = cz::sysex::encode(tone);
        std::ofstream f(dir / (name + ".syx"), std::ios::binary);
        f.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        midi(dir / (name + ".mid"), notes, sec, burst);
        manifest << name << '\t';
        for (auto n : notes)
            manifest << n << ',';
        manifest << '\t' << sec << '\t' << description << '\n';
    };
    cz::Tone cosine;
    cosine.line[0].env[1].level[0] = 0;
    cosine.line[0].env[2].rate[1] = 99;
    auto silence = cosine;
    silence.line[0].env[2].level[0] = 0;
    save("a-silence", silence, {60}, 4, "Noise floor; no gain normalization");
    for (int level : {25, 50, 75, 99}) {
        auto t = cosine;
        t.line[0].env[2].level[0] = level;
        save("b-cosine-level-" + std::to_string(level), t, {60}, 4, "Converter law against DCA level");
    }
    save("c-bursts", cosine, {60}, .01, "Eight 10 ms bursts, separated by 500 ms", true);
    const std::vector<int> chord{48, 52, 55, 60, 64, 67, 72, 76};
    for (int count : {1, 2, 4, 8})
        save("d-poly-" + std::to_string(count), cosine, {chord.begin(), chord.begin() + count}, 4,
             "Summed versus per-slot converter test");
    for (int note : {36, 48, 60, 72, 84}) {
        auto t = cosine;
        t.line[0].env[1].level[0] = 99;
        save("e-saw-note-" + std::to_string(note), t, {note}, 3, "High-pitch DCW limit and output filter");
    }
    for (int kind = 0; kind < 3; ++kind)
        for (int value : {0, 11, 22, 33, 44, 55, 66, 77, 88, 99})
            for (int rate = 0; rate < 2; ++rate) {
                auto t = cosine;
                auto &env = t.line[0].env[kind];
                env.end = 1;
                env.sustain = 0;
                env.level[0] = rate ? (kind == 0 ? 66 : 99) : value;
                env.rate[0] = rate ? value : 99;
                save("f-env-" + std::to_string(kind) + (rate ? "-rate-" : "-level-") + std::to_string(value),
                     t, {kind == 0 ? 36 : 60}, rate ? (value == 0 ? 120 : (value <= 22 ? 30 : 8)) : 8,
                     "DCO=0, DCW=1, DCA=2; compare segment slope/target");
            }
    for (int value : {0, 25, 50, 75, 99})
        for (int rate = 0; rate < 2; ++rate) {
            auto t = cosine;
            t.vibRate = rate ? value : 50;
            t.vibDepth = rate ? 50 : value;
            save("g-vibrato-" + std::string(rate ? "rate-" : "depth-") + std::to_string(value), t, {60}, 8,
                 "Vibrato curve and firmware update cadence");
        }
    for (int key : {0, 5, 9})
        for (int note : {36, 60, 84}) {
            auto t = cosine;
            t.line[0].env[1].level[0] = 99;
            t.line[0].env[2].rate[0] = 55;
            t.line[0].dcwKey = t.line[0].dcaKey = key;
            save("h-key-" + std::to_string(key) + "-note-" + std::to_string(note), t, {note}, 6,
                 "DCW level and DCA time key follow");
        }
    auto ring = cosine;
    ring.mode = 3;
    ring.detune = 7;
    ring.line[1].wave = 5;
    ring.line[1].env[1].level[0] = 99;
    ring.mod = 1;
    save("i-ring-pair", ring, {60}, 4, "Ring pair; compare each partner alone");
    ring.mod = 0;
    ring.mode = 0;
    save("i-line1", ring, {60}, 4, "Partner one alone");
    ring.mode = 1;
    save("i-line2", ring, {67}, 4, "Partner two at its detuned pitch");
    auto chime = ring;
    chime.mode = 3;
    chime.mod = 1;
    chime.detune = 7;
    for (auto &l : chime.line) {
        l.env[2].sustain = -1;
        l.env[2].rate[1] = 55;
    }
    save("original-detuned-chime", chime, {60, 64, 67, 72}, 2, "Original demonstration, not a factory tone");
}
int main(int argc, char **argv) {
    if (argc >= 2 && std::string(argv[1]) == "--events") {
        try {
            if (argc != 6 && argc != 7)
                throw std::runtime_error("Usage: render --events tone.syx schedule.tsv out.wav "
                                         "native|instant|syllabic [hold|clean]");
            std::string reconstruction = argc == 7 ? argv[6] : "hold";
            if (reconstruction != "hold" && reconstruction != "clean")
                throw std::runtime_error("Reconstruction must be hold or clean");
            std::ifstream f(argv[2], std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
            cz::Tone tone;
            std::string error;
            if (!cz::sysex::decode(bytes, tone, error))
                throw std::runtime_error(error);
            return calibration::renderSequence(tone, argv[3], argv[4], argv[5], reconstruction == "clean");
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 3 && std::string(argv[1]) == "--capture") {
        captures(argv[2]);
        return 0;
    }
    if (argc < 3) {
        std::cerr << "Usage: render tone.syx out.wav [note=60] [seconds=4] [native]\n       render --capture "
                     "directory\n";
        return 1;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    cz::Tone tone;
    std::string error;
    if (!cz::sysex::decode(bytes, tone, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    int note = argc > 3 ? std::stoi(argv[3]) : 60;
    double seconds = argc > 4 ? std::stod(argv[4]) : 4;
    bool native = argc > 5 && std::string(argv[5]) == "native";
    int sr = native ? 40000 : 48000;
    int frames = int(std::clamp(seconds, .1, 300.) * sr);
    std::ofstream out(argv[2], std::ios::binary);
    out.write("RIFF", 4);
    le(out, 36 + frames * 2, 4);
    out.write("WAVEfmt ", 8);
    le(out, 16, 4);
    le(out, 1, 2);
    le(out, 1, 2);
    le(out, sr, 4);
    le(out, sr * 2, 4);
    le(out, 2, 2);
    le(out, 16, 2);
    out.write("data", 4);
    le(out, frames * 2, 4);
    auto engine = std::make_unique<cz::VoiceEngine>();
    std::array<cz::Input, 8> in{};
    in[0].pitch = (note - 60) / 12.f;
    cz::RenderSettings settings;
    settings.polyEnabled = false;
    double energy = 0, peak = 0;
    for (int i = 0; i < frames; ++i) {
        in[0].gate = i < int(frames * .65) ? 10 : 0;
        float x = engine->process(in, 1, tone, 0, sr, true, settings);
        if (native) {
            int sum = 0;
            for (auto y : engine->chip.lastVoices)
                sum += y;
            x = float(sum) / 32768.f;
        } else
            x *= 8;
        x = std::clamp(x, -1.f, 1.f);
        le(out, uint16_t(int16_t(std::lround(x * 32767))), 2);
        peak = std::max(peak, std::abs(double(x)));
        energy += x * x;
    }
    std::cout << "Rendered " << frames << " frames at " << sr << " Hz; peak " << peak << ", RMS "
              << std::sqrt(energy / frames) << '\n';
    return out ? 0 : 1;
}
