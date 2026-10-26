// SPDX-License-Identifier: BSD-3-Clause
// Original sounds, a short pattern, then deliberately exaggerated per-hit edits.
#include "../src/engine/Percussion.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <chrono>

static void u16(std::ostream& f, uint16_t v) { f.put(char(v)); f.put(char(v >> 8)); }
static void u32(std::ostream& f, uint32_t v) { u16(f, uint16_t(v)); u16(f, uint16_t(v >> 16)); }

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: render output.wav\n"; return 2; }
    constexpr int sr = 48000, seconds = 34, frames = seconds * sr;
    std::ofstream out(argv[1], std::ios::binary);
    if (!out) return 2;
    out.write("RIFF", 4); u32(out, 36 + frames * 4); out.write("WAVEfmt ", 8);
    u32(out, 16); u16(out, 1); u16(out, 2); u32(out, sr); u32(out, sr * 4); u16(out, 4); u16(out, 16);
    out.write("data", 4); u32(out, frames * 4);
    drumlab::Engine engine;
    auto began = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        double time = double(i) / sr;
        if (i < 12 * sr) {
            if (i % (2 * sr) == 0) {
                drumlab::Hit h; h.machine = drumlab::Machine(i / (2 * sr));
                h.decay = h.machine == drumlab::Machine::Hat ? .16 : .6;
                engine.trigger(0, h, sr);
            }
        } else if (i < 32 * sr) {
            constexpr int stepFrames = 6000; // 120 BPM sixteenths
            int elapsed = i - 12 * sr, step = elapsed / stepFrames;
            if (elapsed % stepFrames == 0) {
                for (int c = 0; c < 4; ++c) {
                    bool on = c == 0 ? step % 4 == 0 || step % 16 == 14 :
                              c == 1 ? step % 8 == 4 : c == 2 ? step % 2 == 0 : step % 8 == 3 || step % 16 == 15;
                    if (!on) continue;
                    drumlab::Hit h; h.machine = drumlab::Machine(c);
                    h.decay = c == 0 ? .4 : c == 1 ? .2 : c == 2 ? .09 : .3;
                    h.color = c == 2 ? .8 : .45;
                    h.velocity = c == 2 ? .55 : c == 3 ? .7 : .9;
                    if (time >= 20) {
                        h.pitch = c == 3 ? double((step * 5) % 12) / 12 : (step % 8 == 6 ? -.5 : 0);
                        h.color = double((step * 7 + c * 13) % 17) / 16;
                        h.contour = double((step + c) % 9) / 8;
                        h.bits = step % 16 >= 12 ? 5 : 16;
                        h.reduction = step % 16 >= 12 ? 7 : 1;
                        h.decay *= step % 8 == 0 ? 2 : 1;
                    }
                    engine.trigger(c, h, sr);
                }
            }
        }
        double left = 0, right = 0;
        for (int c = 0; c < 4; ++c) {
            double x = engine.tick(c, sr);
            double pan = c == 2 ? -.45 : c == 3 ? .45 : 0;
            double angle = (pan + 1) * drumlab::pi / 4;
            left += x * std::cos(angle) * .55;
            right += x * std::sin(angle) * .55;
        }
        for (double x : {left, right}) {
            int sample = int(std::lround(std::clamp(x, -1., 1.) * 32767));
            u16(out, uint16_t(int16_t(sample)));
        }
    }
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    std::cout << "Rendered " << seconds << " seconds in " << elapsed << " s: " << argv[1] << '\n';
    return out ? 0 : 2;
}
