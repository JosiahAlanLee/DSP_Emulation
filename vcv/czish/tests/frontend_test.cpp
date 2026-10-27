#include "../src/engine/CzVoice.hpp"
#include "../src/engine/Sysex.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <vector>
using namespace dspemu;
static void run(cz::VoiceEngine &e, std::array<cz::Input, 8> &in, int count, cz::Tone &t, int frames) {
    for (int i = 0; i < frames; ++i)
        assert(std::isfinite(e.process(in, count, t, 0, 48000, true)));
}
static void envelopes() {
    cz::VoiceEngine e;
    cz::Tone t;
    std::array<cz::Input, 8> in{};
    in[0].gate = 10;
    auto &env = t.line[0].env[2];
    env.rate.fill(99);
    env.level = {99, 60, 0, 0, 0, 0, 0, 0};
    env.sustain = 1;
    env.end = 2;
    run(e, in, 1, t, 1000);
    assert(e.runs[0][2].step == 1 && !e.runs[0][2].done);
    assert(e.chip.dca[0].current == uint32_t(60) << 18);
    in[0].gate = 0;
    run(e, in, 1, t, 1000);
    assert(e.runs[0][2].step == 2 && e.runs[0][2].done && e.chip.dca[0].current == 0);
    in[0].gate = 10;
    run(e, in, 1, t, 2);
    assert(e.runs[0][2].step == 0);
    in[0].gate = 0;
    run(e, in, 1, t, 1000);
    assert(e.chip.dca[0].current == 0);
    in[0].gate = 10;
    env.end = 7;
    env.sustain = -1;
    env.level = {20, 40, 60, 80, 99, 60, 30, 0};
    run(e, in, 1, t, 4000);
    assert(e.runs[0][2].step == 7 && e.runs[0][2].done);
}
static void modes() {
    for (int mode = 0; mode < 4; ++mode) {
        cz::VoiceEngine e;
        cz::Tone t;
        t.mode = mode;
        t.line[1].wave = 2;
        t.line[0].alternate = true;
        t.line[0].second = 4;
        t.mod = 1;
        std::array<cz::Input, 8> in{};
        for (int n = 0; n < 8; ++n) {
            in[n].gate = 10;
            in[n].pitch = n / 12.f;
        }
        run(e, in, 8, t, 1000);
        assert(e.activeChannels == (mode < 2 ? 8 : 4));
        if (mode < 2) {
            for (int n = 0; n < 8; ++n) {
                assert(e.chip.voice[n].wave[0] == (mode ? 2 : 0));
                assert(!e.chip.voice[n].ringMod);
            }
        } else
            for (int n = 0; n < 4; ++n) {
                int a = cz::VoiceEngine::first[n], b = cz::VoiceEngine::second[n];
                assert(e.chip.voice[b].ringMod);
                assert(e.chip.voice[a].wave[1] == 4);
                assert(e.chip.voice[b].wave[0] == (mode == 2 ? 0 : 2));
            }
        if (mode >= 2) {
            t.detune = 12;
            t.fine = 30;
            run(e, in, 8, t, 100);
            assert(e.chip.voice[0].pitch > e.chip.voice[5].pitch);
            t.mod = 2;
            run(e, in, 8, t, 100);
            for (int n : {0, 2, 4, 6})
                assert(e.chip.voice[n].pitchMod == 3);
        }
    }
}
static void tuningAndControls() {
    cz::Upd933 c;
    for (int semitone = -48; semitone <= 48; ++semitone) {
        float v = semitone / 12.f;
        c.write(0x60, cz::pitchRegister(v));
        double hz = double(c.voice[0].pitchStep) * 40000. / (double(1u << 20) * 2048.);
        double target = 261.6255653005986 * std::exp2(v);
        assert(std::abs(1200 * std::log2(hz / target)) < .61);
    }
    cz::VoiceEngine e;
    cz::Tone t;
    t.vibDepth = 50;
    t.vibDelay = 10;
    std::array<cz::Input, 8> in{};
    in[0].gate = 10;
    run(e, in, 1, t, 600);
    assert(e.chip.voice[0].pitch == cz::pitchRegister(0));
    run(e, in, 1, t, 1600);
    assert(e.chip.voice[0].pitch != cz::pitchRegister(0));
    t.vibrato = false;
    t.line[0].dcwKey = 9;
    in[0].pitch = 2;
    in[0].dcw = 0;
    run(e, in, 1, t, 1000);
    assert(e.chip.dcwOffset[0] < 0);
    in[0].pitch = -7;
    in[0].dcw = 100;
    run(e, in, 1, t, 1000);
    assert(std::isfinite(e.polySamples[0]));
}
static void sysex() {
    // Anchors from printed tables, independent of codec round-trip.
    assert(cz::rateCode(cz::EnvKind::Dca, 99) == 0x77);
    assert(cz::rateCode(cz::EnvKind::Dcw, 0) == 8);
    assert(cz::rateCode(cz::EnvKind::Dco, 99) == 0x7f);
    assert(cz::levelCode(cz::EnvKind::Dco, 66) == 0x46);
    assert(cz::levelCode(cz::EnvKind::Dco, 99) == 0x67);
    assert(cz::levelCode(cz::EnvKind::Dca, 99) == 0x63);
    assert(cz::sysex::vibratoValue(0, 64) == 0x87);
    assert(cz::sysex::vibratoValue(1, 48) == 0x8e0);
    assert(cz::sysex::vibratoValue(1, 99) == 0x53e0);
    assert(cz::sysex::vibratoValue(2, 12) == 13);
    assert(cz::sysex::vibratoValue(2, 99) == 0x300);
    cz::Tone t;
    t.mode = 3;
    t.octave = -1;
    t.detune = 25;
    t.fine = 60;
    t.vibRate = 99;
    t.vibDepth = 99;
    t.vibDelay = 64;
    t.line[0].wave = 6;
    t.line[0].window = 3;
    auto payload = cz::sysex::encodePayload(t);
    assert(payload[0] == 11 && payload[2] == 252 && payload[3] == 25 && payload[4] == 8);
    assert(payload[5] == 64 && payload[6] == 0 && payload[7] == 0x87);
    assert(payload[8] == 99 && payload[9] == 0x53 && payload[10] == 0xe0);
    assert(payload[12] == 3 && payload[13] == 0);
    assert(payload[14] == 0xc0 && payload[15] == 0xc0); // wave 6 << 13, window 3 << 6, little-endian word
    t.vibWave = 2;
    t.line[0].wave = 5;
    t.line[0].second = 1;
    t.line[0].alternate = true;
    t.line[0].window = 0;
    payload = cz::sysex::encodePayload(t);
    assert(payload[4] == 0x20 && payload[14] == 0 && payload[15] == 0xa6);
    t.vibWave = 3;
    assert(cz::sysex::encodePayload(t)[4] == 2);
    std::mt19937 random(0xc2101);
    for (int test = 0; test < 300; ++test) {
        t.mode = random() % 4;
        t.octave = int(random() % 3) - 1;
        t.fine = random() % 61;
        t.vibWave = random() % 4;
        for (auto &line : t.line) {
            line.wave = random() % 8;
            line.second = random() % 8;
            line.window = random() % 8;
            line.alternate = random() % 2;
            line.dcaKey = random() % 10;
            line.dcwKey = random() % 10;
            for (auto &env : line.env) {
                env.end = random() % 8;
                env.sustain = int(random() % (env.end + 2)) - 1;
                for (int i = 0; i < 8; ++i) {
                    env.rate[i] = random() % 100;
                    env.level[i] = i == env.end ? 0 : random() % 100;
                }
            }
        }
        auto data = cz::sysex::encode(t, test % 16);
        cz::Tone decoded;
        std::string error;
        assert(cz::sysex::decode(data, decoded, error));
        assert(cz::sysex::encode(decoded) == data);
        assert(decoded.mode == t.mode && decoded.octave == t.octave && decoded.fine == t.fine &&
               decoded.vibWave == t.vibWave);
        // The older guide prints high-first nibbles; accept and retain that form too.
        for (size_t i = 6; i < 262; i += 2)
            std::swap(data[i], data[i + 1]);
        assert(cz::sysex::decode(data, decoded, error));
        assert(decoded.sourceHighNibbles);
        assert(cz::sysex::encode(decoded) == data);
    }
    auto data = cz::sysex::encode(cz::Tone{});
    std::string error;
    cz::Tone decoded;
    for (size_t n = 0; n < data.size(); ++n) {
        std::vector<uint8_t> truncated(data.begin(), data.begin() + n);
        assert(!cz::sysex::decode(truncated, decoded, error));
    }
    data[40] = 0x10;
    assert(!cz::sysex::decode(data, decoded, error));
}
static double component(const std::vector<float> &a, double f, double rate) {
    double r = 0, i = 0;
    for (size_t n = 0; n < a.size(); ++n) {
        double p = 2 * rate::pi * f * n / rate;
        r += a[n] * std::cos(p);
        i -= a[n] * std::sin(p);
    }
    return 2 * std::hypot(r, i) / a.size();
}
static void converter() {
    for (int bits = 4; bits <= 16; ++bits)
        for (int v = -32768; v < 32768; v += 31) {
            int step = 1 << (16 - bits);
            auto y = dac::linearRoundTrip(v, bits, true);
            assert(std::abs(int(y) - v) <= step || y == 32767);
        }
    for (double device : {1000., 12000., 24000., 48000.}) {
        dac::Stage raw, filtered;
        std::vector<float> a, b;
        double f = device == 48000 ? 1000 : device * .75, alias = device - f;
        for (int i = 0; i < 96000; ++i) {
            float x = float(.8 * std::sin(2 * rate::pi * f * i / 48000));
            float y = raw.process(x, dac::Profile::Linear, 16, 0, 0, device, 48000, false, true);
            float z = filtered.process(x, dac::Profile::Linear, 16, 0, 0, device, 48000, true, true);
            if (i >= 48000) {
                a.push_back(y);
                b.push_back(z);
            }
        }
        if (device < 48000) {
            double u = component(a, alias, 48000), v = component(b, alias, 48000);
            std::cout << "AA " << device << ": " << u << " -> " << v << "\n";
            assert(u > .05 && v < u * .001);
        }
    }
    for (double device : {16000., 16013., 40000., 55930.}) {
        dac::Stage s;
        std::vector<float> a;
        double freq = 997;
        for (int i = 0; i < 96000; ++i) {
            float x = float(.8 * std::sin(2 * rate::pi * freq * i / 48000));
            float y = s.process(x, dac::Profile::Linear, 16, 0, 0, device, 48000, false, false);
            if (i >= 48000)
                a.push_back(y);
        }
        double fundamental = component(a, freq, 48000);
        if (device < 20000) {
            double image = component(a, device - freq, 48000);
            double expected = freq / (device - freq);
            double db = 20 * std::log10(image / fundamental / expected);
            std::cout << "HOLD image " << device << ": " << db << " dB error\n";
            assert(std::abs(db) < .5);
        } else {
            double folded = std::abs(48000 - (device - freq));
            if (folded < 24000) {
                double unwanted = component(a, folded, 48000);
                std::cout << "HOLD rejected " << device << ": " << unwanted / fundamental << "\n";
                assert(unwanted < fundamental * .001);
            }
        }
    }
    dac::Stage clean;
    double error = 0, power = 0;
    for (int i = 0; i < 96000; ++i) {
        float x = float(.8 * std::sin(2 * rate::pi * 12000 * i / 48000));
        float y = clean.process(x, dac::Profile::Linear, 16, 0, 0, 48000, 48000, true, true);
        if (i > 48000) {
            double ref = .8 * std::sin(2 * rate::pi * 12000 * (i - 62) / 48000);
            error += (y - ref) * (y - ref);
            power += ref * ref;
        }
    }
    std::cout << "16-bit CLEAN error: " << 10 * std::log10(error / 47999) << " dBFS RMS ("
              << 10 * std::log10(error / power) << " dB relative)\n";
    assert(error / 47999 < 1e-9);
    dac::Stage noisy;
    noisy.noiseDb = -60;
    double energy = 0;
    for (int i = 0; i < 96000; ++i) {
        float y = noisy.process(0, dac::Profile::Linear, 16, 0, 0, 48000, 48000, false, true);
        if (i > 48000)
            energy += y * y;
    }
    assert(energy / 48000 > 5e-7 && energy / 48000 < 2e-6);
    dac::CzCompander compander;
    auto instant = compander.process(32760, dac::CzModel::Instant, 0, 0, 40000);
    auto first = compander.process(32760, dac::CzModel::Syllabic, 0, 0, 40000);
    assert(instant.value > first.value);
}
int main() {
    envelopes();
    modes();
    tuningAndControls();
    sysex();
    converter();
    std::cout << "PASS: complete front end, documented MIDI maps, SysEx framing/roundtrip, minimum-phase "
                 "HOLD, extreme-ratio AA, transparency and noise\n";
}
