// SPDX-License-Identifier: BSD-3-Clause
// ES5510-style number handling, modelled on MAME's ES5510 core (es5510.cpp,
// BSD-3-Clause, Christian Brunschen) at commit 016b077. This models the chip's
// arithmetic limits; it does not execute ES5510 programs.
#pragma once
#include <cmath>
#include <cstdint>

namespace spoonbender {

constexpr int32_t MAX24 = 0x7FFFFF, MIN24 = -0x800000;
constexpr double Q23 = 8388608.0;  // 2^23: full scale of a 24-bit signed word

// Saturate to 24 bits, as MAME's saturate() and saturating MAC reads do.
inline int32_t sat24(int64_t x) { return x > MAX24 ? MAX24 : x < MIN24 ? MIN24 : int32_t(x); }
// Interpret the low 24 bits of x as a signed value (shifts on unsigned only).
inline int32_t sext24(uint32_t x) { x &= 0xFFFFFFu; return int32_t(x ^ 0x800000u) - 0x800000; }
// 24-bit value to a 16-bit delay word: drop 8 bits rounding toward -infinity,
// MAME's default DOL behaviour. Arithmetic right shift of a negative int32_t is
// implementation-defined before C++20; GCC, Clang and MSVC all shift arithmetically.
inline int16_t toDelayWord(int32_t v24) { return int16_t(v24 >> 8); }
inline int32_t fromDelayWord(int16_t w) { return int32_t(w) * 256; }
// Q23 x Q23 -> Q23 through a 48-bit product, saturating on overflow: MAME's MAC
// write-back (bits 24..47) with mulshift = 1 (SIGREG bit 22 set) and no accumulate.
inline int32_t mul24(int32_t a, int32_t b) { return sat24((int64_t(a) * b) >> 23); }

// Float <-> 24-bit with rounding toward -infinity (how the MAC's high word truncates).
inline int32_t toQ23(float x) {
    if (!std::isfinite(x)) return 0;
    double v = std::floor(double(x) * Q23);
    return v >= MAX24 ? MAX24 : v <= MIN24 ? MIN24 : int32_t(v);
}
inline float fromQ23(int32_t v) { return float(v / Q23); }

// Numeric policies the algorithms are written against. Every summing node calls
// node(); every delay-memory write calls store(). CLEAN leaves values alone;
// ESP quantizes nodes to saturating 24-bit and memory to 16-bit words.
struct CleanMath {
    static constexpr bool esp = false;
    static float node(float x) { return x; }
    static float store(float x) { return std::abs(x) < 1e-20f ? 0.f : x; }  // no denormals in memory
    static float coef(float c) { return c; }
};
// Same results as fromQ23(toQ23(x)) and fromQ23(fromDelayWord(toDelayWord(toQ23(x)))),
// computed in float: scaling by a power of two and floor() are exact, and the
// nested floor of a division by 256 equals one floor at 2^15. Checked against the
// integer helpers in the tests. NaN is handled at the engine output.
struct EspMath {
    static constexpr bool esp = true;
    static float node(float x) {
        float v = std::floor(x * 8388608.f);
        v = v < -8388608.f ? -8388608.f : v > 8388607.f ? 8388607.f : v;
        return v * (1.f / 8388608.f);
    }
    static float store(float x) {
        float v = std::floor(x * 32768.f);
        v = v < -32768.f ? -32768.f : v > 32767.f ? 32767.f : v;
        return v * (1.f / 32768.f);
    }
    static float coef(float c) { return node(c); }
};

} // namespace spoonbender
