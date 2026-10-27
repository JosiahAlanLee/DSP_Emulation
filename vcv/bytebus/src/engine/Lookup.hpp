// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "../../../shared/ByteBus.hpp"
#include "LibraryIdentity.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace dspemu::lookup {
constexpr int FAMILIES = 6, V2_FAMILIES = 3, V3_FAMILIES = 1, SHAPES = 64, POINTS = 256;
// Library v2's and v3's families follow Custom, so FAMILY values saved before them are unchanged.
enum Family { Harmonic, Formant, Fold, Curve, Walsh, Rhythm, Custom, Envelope, Sequence, Density, Ratio };
inline bool inV2(int family) { return family > Custom && family <= Density; }
inline bool inV3(int family) { return family == Ratio; }
// A ratio word (Ratio's output, Counter 2's RATIO input): n - 1 in the high nibble, d - 1 in the low.
inline int ratioN(uint8_t word) { return (word >> 4) + 1; }
inline int ratioD(uint8_t word) { return (word & 15) + 1; }
enum class Mode { Map, TrackRising, TrackFalling };
inline uint64_t hash(const uint8_t* bytes, size_t size) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i) { h ^= bytes[i]; h *= 1099511628211ull; }
    return h;
}
inline uint32_t mix(uint32_t v) {
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu;
    return v ^ (v >> 16);
}
inline double wrap(double v, double range) { return v - range * std::floor(v / range); }
inline uint8_t rounded(double v) { return uint8_t(std::max(0., std::min(255., std::floor(v + .5)))); }
using Table = std::array<uint8_t, POINTS>;
struct Library {
    std::array<uint8_t, (FAMILIES + V2_FAMILIES + V3_FAMILIES) * SHAPES * POINTS> data{};
    bool ready = false, readyV2 = false, readyV3 = false;
    static Library load(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        std::array<char, 12> header{}; Library library;
        if (!file.read(header.data(), header.size()) || std::string(header.data(), 8) != "BBLOOK01" ||
            uint8_t(header[8]) != FAMILIES || uint8_t(header[9]) != SHAPES || header[10] != 0 || header[11] != 1)
            throw std::runtime_error("Missing or unsupported Lookup v1 library");
        constexpr size_t size = FAMILIES * SHAPES * POINTS;
        if (!file.read(reinterpret_cast<char*>(library.data.data()), size) || file.peek() != EOF ||
            hash(library.data.data(), size) != LIBRARY_HASH)
            throw std::runtime_error("Lookup v1 library fingerprint mismatch");
        library.ready = true; return library;
    }
    // Library v2 (Envelope, Sequence, Density), a separate identity; v1 is unchanged by it.
    void loadV2(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        std::array<char, 12> header{};
        if (!file.read(header.data(), header.size()) || std::string(header.data(), 8) != "BBLOOK02" ||
            uint8_t(header[8]) != V2_FAMILIES || uint8_t(header[9]) != SHAPES || header[10] != 0 || header[11] != 2)
            throw std::runtime_error("Missing or unsupported Lookup v2 library");
        constexpr size_t offset = FAMILIES * SHAPES * POINTS, size = V2_FAMILIES * SHAPES * POINTS;
        if (!file.read(reinterpret_cast<char*>(data.data() + offset), size) || file.peek() != EOF ||
            hash(data.data() + offset, size) != LIBRARY_V2_HASH)
            throw std::runtime_error("Lookup v2 library fingerprint mismatch");
        readyV2 = true;
    }
    // Library v3 (Ratio), a further identity.
    void loadV3(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        std::array<char, 12> header{};
        if (!file.read(header.data(), header.size()) || std::string(header.data(), 8) != "BBLOOK03" ||
            uint8_t(header[8]) != V3_FAMILIES || uint8_t(header[9]) != SHAPES || header[10] != 0 || header[11] != 3)
            throw std::runtime_error("Missing or unsupported Lookup v3 library");
        constexpr size_t offset = (FAMILIES + V2_FAMILIES) * SHAPES * POINTS, size = V3_FAMILIES * SHAPES * POINTS;
        if (!file.read(reinterpret_cast<char*>(data.data() + offset), size) || file.peek() != EOF ||
            hash(data.data() + offset, size) != LIBRARY_V3_HASH)
            throw std::runtime_error("Lookup v3 library fingerprint mismatch");
        readyV3 = true;
    }
    bool has(int family) const { return inV3(family) ? readyV3 : inV2(family) ? readyV2 : ready; }
    const uint8_t* table(int family, int shape) const {
        int index = family > Custom ? family - 1 : family;   // Custom has no stored table
        return data.data() + (index * SHAPES + shape) * POINTS;
    }
};

// Explicit ramp follower. Three wraps establish two consistent periods; unrelated
// backwards motion prevents lock. Mapping remains available while unlocked.
struct Follower {
    double time = 0, lastWrap = 0, candidate = 0, period = 0;
    int previous = 0, forward = 0;
    bool seen = false, haveWrap = false, dirty = false, validPeriod = false, locked = false;
    void reset() { *this = Follower{}; }
    double tick(uint8_t word, bool observed, bool falling, double sampleRate) {
        double sr = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 48000.;
        time += 1 / sr;
        int value = falling ? 255 - int(word) : int(word);
        if (observed) {
            if (seen) {
                int difference = value - previous;
                if (difference < -64) {
                    double elapsed = time - lastWrap;
                    bool valid = haveWrap && !dirty && forward >= 2 && elapsed >= 4 / sr && elapsed <= 120.;
                    if (valid) {
                        locked = validPeriod && std::abs(elapsed - candidate) <= .25 * std::max(elapsed, candidate);
                        period = locked ? (elapsed + candidate) * .5 : elapsed;
                        candidate = elapsed; validPeriod = true;
                    } else { locked = false; validPeriod = false; }
                    lastWrap = time; haveWrap = true; dirty = false; forward = 0;
                } else if (difference < -4 || difference > 64) dirty = true;
                else if (difference > 0) ++forward;
            }
            previous = value; seen = true;
        }
        if (locked && time - lastWrap > 2.5 * period) {
            locked = false; validPeriod = false; haveWrap = false;
        }
        return locked ? wrap((time - lastWrap) / period, 1.) * 256 : word;
    }
};

struct Selector {
    int selected = 0; bool initialized = false;
    int choose(double shape) {
        shape = wrap(shape, 64.);
        double distance = wrap(shape - selected + 32., 64.) - 32.;
        if (!initialized || std::abs(distance) > .55) {
            selected = int(std::floor(shape + .5)) & 63; initialized = true;
        }
        return selected;
    }
};
// JUMP (docs/LOOKUP.md): a persistent SHAPE step of one position on each publication of the JUMP
// bus where the selected bit is high, the Machinemodule BUS TRIG rule. Clocked: each CLK rising
// edge with the bit high steps, and the first edge after a fresh start only arms, since a reader
// counts a CLK that is already high as an edge. Unclocked: each rising edge of the bit steps, and
// a bit already high when the cable arrives, or when the bit selection changes, does not.
struct Jump {
    int steps = 0;                     // 0-63, added to SHAPE; RESET clears it
    bool connected = false, clocked = false, armed = false, high = false;
    int bit = -1;
    void clear() { *this = Jump{}; }
    // Detectors start over; the step count stays.
    void restart() { int kept = steps; clear(); steps = kept; }
    bool tick(const bus::Sample& in, int selectedBit) {
        bool fresh = in.connected != connected || in.clocked != clocked;
        connected = in.connected; clocked = in.clocked;
        if (!in.connected) { restart(); return false; }
        bool level = in.rawWord >> selectedBit & 1, step = false;
        if (in.clocked) {
            if (fresh) armed = false;
            if (in.rising) { step = armed && level; armed = true; }
        } else {
            if (fresh || selectedBit != bit) high = level;
            step = level && !high; high = level;
        }
        bit = selectedBit;
        if (step) steps = (steps + 1) & 63;
        return step;
    }
};
struct Parameters {
    int family = Harmonic;
    double shape = 0, phase = 0; // shape 0..64 circular; phase in cycles
    uint8_t seed = 0; // Pattern seed: Walsh polarity permutation / rhythm ghosts.
    Mode mode = Mode::Map;
    bool engineClock = false;
};
struct Output {
    uint8_t word = 128, address = 128;
    bool clocked = false, clk = false, locked = false;
};
inline double point(const uint8_t* table, double address) {
    int a = int(std::floor(address)) & 255;
    double f = address - std::floor(address);
    return table[a] * (1 - f) + table[(a + 1) & 255] * f;
}
inline uint8_t evaluate(const Library& library, const Parameters& p, double address, int selected, const Table* custom = nullptr) {
    address = wrap(address, 256.);
    if (p.family == Custom) {
        double identity = address <= 255 ? address : 255 * (256 - address);
        double value = custom ? point(custom->data(), address) : identity;
        double amount = .5 - .5 * std::cos(2 * 3.14159265358979323846 * wrap(p.shape, 64.) / 64.);
        return rounded((1 - amount) * identity + amount * value);
    }
    if (!library.has(p.family)) return 128;
    if (p.family == Walsh) return library.table(Walsh, selected)[int(address) ^ p.seed];
    if (p.family == Rhythm) {
        int a = int(address), step = a / 8;
        if (a % 8 >= 4) return 0; // A low half-step separates adjacent triggers.
        uint8_t word = library.table(Rhythm, selected)[a];
        if (p.seed) for (int lane = 2; lane < 8; ++lane)
            if (int(mix(uint32_t(p.seed) * 257u + selected * 17u + lane * 65537u) & 31u) == step)
                word ^= uint8_t(1u << lane);
        return word;
    }
    // Density's tables already carry Rhythm's low half-step; each shape is one more hit. Ratio words
    // can't be blended either: both read a whole address of a discretely selected shape.
    if (p.family == Density || p.family == Ratio) return library.table(p.family, selected)[int(address)];
    double shape = wrap(p.shape, 64.); int a = int(shape), b = (a + 1) & 63;
    double mixAmount = shape - a;
    return rounded((1 - mixAmount) * point(library.table(p.family, a), address) +
                   mixAmount * point(library.table(p.family, b), address));
}

struct Engine {
    Follower follower; Selector selector;
    uint8_t held = 128; int lastFamily = -1; Mode lastMode = Mode::Map;
    void reset() { follower.reset(); selector = {}; held = 128; lastFamily = -1; }
    Output tick(const Library& library, Parameters p, const bus::Sample& input, double sampleRate, bool resetFollower = false, const Table* custom = nullptr) {
        p.family = std::max(0, std::min(int(Ratio), p.family));
        if (!std::isfinite(p.shape)) p.shape = 0;
        if (!std::isfinite(p.phase)) p.phase = 0;
        bool tracking = p.mode != Mode::Map;
        if (p.mode != lastMode || resetFollower) follower.reset();
        lastMode = p.mode;
        Output out;
        out.clocked = input.clocked && !tracking && !p.engineClock;
        out.clk = out.clocked && input.clk;
        if (!input.connected) { follower.reset(); held = 128; return out; }
        double address = input.rawWord;
        if (tracking) address = follower.tick(input.word, input.updated, p.mode == Mode::TrackFalling, sampleRate);
        out.locked = follower.locked;
        if (out.clocked && !input.rising) { out.word = held; return out; }
        if (p.family != lastFamily) { selector = {}; lastFamily = p.family; }
        address = wrap(address + wrap(p.phase, 1.) * 256., 256.);
        int selected = selector.choose(p.shape);
        held = evaluate(library, p, address, selected, custom);
        out.word = held; out.address = uint8_t(address);
        return out;
    }
};
} // namespace dspemu::lookup
