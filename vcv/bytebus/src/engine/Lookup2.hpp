// SPDX-License-Identifier: BSD-3-Clause
// Two words in, one out: f[family][shape](A, B) -> byte. Every family is computed rather than
// stored, so there is no table library: Logic (the sixteen two-input bit functions), Arithmetic
// (sums, mixes, products, comparisons), Fold (a continuous two-input triangle fold) and Walsh
// (64 linear maps over both words, each one-to-one in A and in B). Transparent on A's clock, as
// Lookup's Map/Follow is. Contract: docs/LOOKUP2.md.
#pragma once
#include "Lookup.hpp"

namespace dspemu::lookup2 {
enum Family { Logic, Arithmetic, Fold, Walsh, FAMILIES };
constexpr int SHAPES = 64;

// Logic and Arithmetic have sixteen shapes each; SHAPE passes through them four times.
inline uint8_t logic(int f, uint8_t a, uint8_t b) {
    switch (f & 15) {
        case 0: return a;                    case 1: return a & b;
        case 2: return a | b;                case 3: return a ^ b;
        case 4: return b;                    case 5: return uint8_t(~(a & b));
        case 6: return uint8_t(~(a | b));    case 7: return uint8_t(~(a ^ b));
        case 8: return uint8_t(a & ~b);      case 9: return uint8_t(~a & b);
        case 10: return uint8_t(a | ~b);     case 11: return uint8_t(~a | b);
        case 12: return uint8_t(~a);         case 13: return uint8_t(~b);
        case 14: return 0;                   default: return 255;
    }
}
inline uint8_t arithmetic(int f, uint8_t a, uint8_t b) {
    int x = a, y = b;
    auto byte = [](int v) { return uint8_t(std::max(0, std::min(255, v))); };
    switch (f & 15) {
        case 0: return uint8_t(x + y);                       // sum, wrapping
        case 1: return byte(x + y - 128);                    // bipolar mix, clipped
        case 2: return uint8_t(x - y);                       // difference, wrapping
        case 3: return uint8_t(std::abs(x - y));             // distance
        case 4: return uint8_t((x * y + 127) / 255);         // product as levels
        case 5:                                              // ring: product as bipolar values, half up
            return byte(128 + int(std::floor(((x - 128) * (y - 128) + 64) / 128.)));
        case 6: return uint8_t((x + y + 1) / 2);             // mean, half up
        case 7: return uint8_t(std::max(x, y));
        case 8: return uint8_t(std::min(x, y));
        case 9: return x > y ? 255 : 0;
        case 10: return x == y ? 255 : 0;
        case 11: return byte(x + y);                         // sum, saturating
        case 12: return byte(x - y);                         // difference, floored at 00
        case 13: return uint8_t((x * y) & 255);              // product, low byte
        case 14: return uint8_t((x * y) >> 8);               // product, high byte
        default: return uint8_t(x % (y + 1));                // remainder
    }
}
// A triangle fold with period 4: the identity on [-1, 1], reflected beyond.
inline double triangle(double v) {
    double t = lookup::wrap((v + 1) * .5, 2.);
    return 1 - 2 * std::abs(t - 1);
}
// Fold: SHAPE turns once around a circle. At 0 the output is A exactly; towards 32 B mixes in,
// the drive rises to 4 and a product term appears, and it all returns by 64.
inline uint8_t fold(double shape, uint8_t a, uint8_t b) {
    double t = 2 * 3.14159265358979323846 * lookup::wrap(shape, 64.) / 64.;
    double x = (a - 128) / 128., y = (b - 128) / 128.;
    double mixB = std::sin(t / 2), drive = 1 + 1.5 * (1 - std::cos(t)), product = .75 * std::sin(t);
    double u = drive * (x + mixB * y) / (1 + mixB) + product * x * y;
    return lookup::rounded(std::min(255., 128 + 128 * triangle(u)));
}
// Walsh: output bit k is the parity of (A | B << 8) & mask[k]. Shape 0 is A XOR B; each later shape
// applies two more reversible row operations to the eight 16-bit masks. Every mask keeps the form
// r | r << 8, so every shape is a fixed byte permutation of A XOR B (equal words give 00) and is
// one-to-one in A for each B, and in B for each A. Kept as released: saved patches' sound.
inline std::array<uint16_t, 8> walshMasks(int shape) {
    std::array<uint16_t, 8> masks{};
    for (int k = 0; k < 8; ++k) masks[k] = uint16_t((1u << k) | (1u << (k + 8)));
    uint32_t state = lookup::mix(uint32_t(shape) + 0x4c4b3257u);
    for (int i = 0; i < shape * 2; ++i) {
        state = lookup::mix(state + 1);
        int a = state & 7, b = (state >> 3) & 7;
        if (a != b) {
            masks[a] ^= masks[b];
            if (state & 64) std::swap(masks[a], masks[b]);
        }
    }
    return masks;
}

struct Parameters {
    int family = Logic;
    double shape = 0;        // 0-64, circular; JUMP's steps are already added
};
struct Output {
    uint8_t word = 0;
    bool clocked = false, clk = false, evaluated = false;
};

struct Engine {
    std::array<std::array<uint16_t, 8>, SHAPES> masks{};
    lookup::Selector selector;
    int lastFamily = -1;
    uint8_t held = 0;
    bool fresh = true;       // no result computed from a clocked A since the reset

    Engine() { for (int s = 0; s < SHAPES; ++s) masks[s] = walshMasks(s); }
    void reset() { selector = {}; lastFamily = -1; held = 0; fresh = true; }
    uint8_t evaluate(int family, double shape, uint8_t a, uint8_t b) {
        if (family == Fold) return fold(shape, a, b);
        if (family != lastFamily) { selector = {}; lastFamily = family; }
        int s = selector.choose(shape);
        if (family == Logic) return logic(s, a, b);
        if (family == Arithmetic) return arithmetic(s, a, b);
        uint16_t both = uint16_t(a | b << 8); uint8_t out = 0;
        for (int k = 0; k < 8; ++k) out |= uint8_t((__builtin_popcount(both & masks[s][k]) & 1) << k);
        return out;
    }
    // A's clock is the output's: on a clocked A the result is evaluated on A's CLK edge from B's raw
    // lines and held between edges; an unclocked or unpatched A is evaluated every sample. After a
    // reset (a reload or Initialize) the first sample of a clocked A is evaluated too, without an edge.
    Output tick(Parameters p, const bus::Sample& a, uint8_t b) {
        p.family = std::max(0, std::min(int(Walsh), p.family));
        if (!std::isfinite(p.shape)) p.shape = 0;
        Output out;
        out.clocked = a.connected && a.clocked;
        out.clk = out.clocked && a.clk;
        if (!out.clocked || a.rising || fresh) { held = evaluate(p.family, p.shape, a.rawWord, b); out.evaluated = true; }
        if (out.clocked) fresh = false;
        out.word = held;
        return out;
    }
};
} // namespace dspemu::lookup2
