// SPDX-License-Identifier: BSD-3-Clause
// PVX: a Polivoks-inspired subtractive voice, portable engine.
//
// A behavioral model built from published descriptions of the Formanta
// Polivoks (see docs/CALIBRATION.md for the sources and every constant that
// is an approximation). No measurements of any owner's unit were used.
//
// Voice chain, per voice, per base sample:
//   VCO1 (saw/pulse) + VCO2 (saw/pulse, detune, hard sync to VCO1) + noise
//     -> mixer -> two-pole state-variable filter (LP/BP) with saturating
//     integrators inside the feedback loop, resonance to self-oscillation
//     and cutoff-dependent drive -> overdrive -> VCA (envelope 2).
//   Envelope 1 shapes the filter cutoff. The filter core, VCOs, overdrive
//   and VCA run at 1x/2x/4x the host rate; a 64-tap windowed-sinc decimator
//   (dspemu::rate::SincHistory, prewarmed in the engine constructor) returns
//   every signal to the host rate, including the LP/BP calibration taps, so
//   nothing bypasses anti-aliasing. Envelopes and cutoff coefficients update
//   at control rate (once per base sample).
//
//   Decimation latency: one base sample at 1x (identity path), ~15 base
//   samples at 2x, ~7 base samples at 4x (the 64-tap kernel's group delay,
//   ~0.31 ms at 2x / ~0.15 ms at 4x at 48 kHz); switching oversample changes
//   the output latency. After reset or note-on the decimator history is
//   zeroed, so a note starts from a clean, ramped onset.
//
// Header-only, Rack-free: it compiles with only the standard library and
// vcv/shared/RateConverter.hpp (the shared BLEP table).
#pragma once
#include "../../../shared/RateConverter.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dspemu::pvx {

struct Input {
    float pitch = 0;  // V/OCT, C4 = 0 V
    float gate = 0;   // > 0.1 V holds a note
    float external = 0;  // direct filter input (the Polivoks' external audio in)
};

struct Parameters {
    // VCOs.
    int osc1Wave = 0, osc2Wave = 0;  // 0 = saw, 1 = pulse
    float osc1Pw = .5f, osc2Pw = .5f;  // pulse duty, clamped to [0.05, 0.95]
    float detune = 0;  // VCO2 offset, semitones
    bool sync = false;  // VCO2 hard sync to VCO1
    float osc1Level = 1, osc2Level = 1, noiseLevel = 0;
    float extLevel = 1;  // external input gain into the filter
    // Envelopes. Mode 0 = AR (attack/release; D and S ignored), 1 = ADSR.
    int envMode = 1;
    float env1Attack = .003f, env1Decay = .3f, env1Sustain = .6f, env1Release = .3f;  // seconds
    float env2Attack = .003f, env2Decay = .25f, env2Sustain = .8f, env2Release = .25f;
    // Filter.
    float cutoff = 1000;  // Hz
    float resonance = 0;  // 0..1; 1 self-oscillates
    float cutoffDrive = 1;  // 0..1 cutoff-dependent filter input drive
    float filterEnv = 0;  // bipolar envelope amount, octaves per unit envelope
    int filterTap = 0;  // 0 = lowpass, 1 = bandpass
    // Output.
    float drive = 0;  // 0..1 overdrive
    float level = 1;
    int oversample = 4;  // 1, 2 or 4
};

// Every provisional constant lives here so a future hardware sweep can fit
// them without touching the structure (see docs/CALIBRATION.md).
struct FilterCalibration {
    float resonanceExponent = 2.2f;  // damping k = 2 * (1 - res)^exponent
    float integ1Saturation = 0.3f;  // tanh argument scale, first integrator
    float integ2Saturation = 0.4f;  // tanh argument scale, second integrator
    float inputDriveScale = 0.5f;  // input-stage gain into the tanh (small-signal)
    float cutoffDriveScale = 1.0f;  // input drive gain added at max cutoff
    float cutoffDrivePower = 1.25f;  // drive rise with cutoff, normalized to 20 kHz
    float resDriveAmount = 0.3f;  // extra input drive per unit resonance
    float overdriveGain = 3.0f;  // tanh argument at drive = 1
    float selfOscSeed = 2e-5f;  // noise floor that starts self-oscillation
    float selfOscThreshold = 0.99f;  // resonance above which the seed is injected
    float selfOscAmount = 0.02f;  // negative damping at full resonance
    float selfOscSharpness = 64.f;  // how late in the pot self-oscillation starts
    float dampingCompression = 0.05f;  // van der Pol term: damping grows with the
                                       // square of the state swing, bounding the
                                       // self-oscillation limit cycle
    float stateClip = 2.5f;  // safety rail only (pathological 1x settings)
    float kickAmplitude = 0.02f;  // filter excitation on each gate edge
    float maxCutoffRatio = 0.1f;  // cutoff / internal (oversampled) rate clamp.
        // The sequential (non-TPT) SVF update here goes linearly unstable at
        // g = 2 sin(pi fc / fs4) >= sqrt(k^2 + 4) - k (at res 0, k = 2:
        // g >= 0.828), where a saturated Nyquist limit cycle also bifurcates
        // in. 0.1 caps g at 2 sin(0.1 pi) = 0.618, comfortably inside the
        // bound with nonlinear margin, and gives a rate-dependent usable
        // cutoff: ~0.1 * host at 1x, ~0.2 * host at 2x, ~0.4 * host at 4x
        // (4.8 / 9.6 / 19.2 kHz at 48 kHz). A larger value is safe up to the
        // stability bound below, which is always enforced.
};

namespace detail {
inline float flushDenormal(float x) {
    return std::abs(x) < 1e-20f ? 0.f : x;
}
// One-pole coefficient for an envelope segment of `seconds`, reaching ~98%
// of its target in about `seconds`.
inline float envPole(float seconds, double sr) {
    double tau = double(std::isfinite(seconds) ? seconds : .01) / 4.;
    tau = std::clamp(tau, 1. / sr, 60.);
    return float(1. - std::exp(-1. / (tau * sr)));
}
inline float noiseValue(uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    // Bipolar: the top 24 bits are uniform in [0, 2^24), scaled to [-1, 1).
    // Mean is -1/2^24 (~ -6e-8), not -0.5.
    return (state >> 8) * (2.f / 16777216.f) - 1.f;
}
} // namespace detail

struct Envelope {
    float level = 0;
    int stage = 0;  // 0 off, 1 attack, 2 decay, 3 sustain, 4 release
    void step(bool gate, bool ad, float attack, float decay, float sustain, float release,
              double sr) {
        if (!gate) {
            if (stage != 0 && stage != 4)
                stage = 4;
            if (stage == 4) {
                level += detail::envPole(release, sr) * (0 - level);
                if (level < 1e-20f)
                    level = 0;
            }
            return;
        }
        if (stage == 0 || stage == 4)
            stage = 1;
        switch (stage) {
        case 1: {
            float a = detail::envPole(attack, sr);
            level += a * (1.0001f - level);
            if (level >= 0.999f) {
                level = 1;
                stage = ad ? 2 : 3;  // AR mode holds at the top until release
            }
            break;
        }
        case 2: {
            float d = detail::envPole(decay, sr);
            level += d * (sustain - level);
            if (level <= sustain + 1e-4f) {
                level = sustain;
                stage = 3;
            }
            break;
        }
        default: break;  // sustain holds
        }
    }
};

struct Voice {
    double phase1 = 0, phase2 = 0;
    dspemu::rate::BlepHold blep1{}, blep2{};
    float lp = 0, bp = 0;
    Envelope env1, env2;
    uint32_t noise = 0x9E3779B9u;
    bool first = true;
    bool gateWasOn = false;
    std::array<float, 4> hist{};
    // Anti-alias decimators: every internal sample is pushed into these; at
    // the base rate each is read back with ratio 1/os. Allocation-free (all
    // storage is fixed arrays); the kernels live in the shared SincBank,
    // which the engine constructor prewarms.
    dspemu::rate::SincHistory decim{}, lpDecim{}, bpDecim{};
    void reset() {
        phase1 = phase2 = 0;
        blep1.reset();
        blep2.reset();
        lp = bp = 0;
        env1 = env2 = {};
        noise = 0x9E3779B9u;
        first = true;
        gateWasOn = false;
        hist = {};
        decim.reset();
        lpDecim.reset();
        bpDecim.reset();
    }
};

class VoiceEngine {
  public:
    static constexpr int voices = 8;
    std::array<Voice, voices> state{};
    uint64_t deviceFrames = 0;

    // Prewarm the shared sinc kernel table so the first process() call never
    // allocates or builds tables on the audio thread.
    VoiceEngine() { (void)dspemu::rate::sincBank(); }

    void reset() {
        for (auto &v : state)
            v.reset();
        deviceFrames = 0;
    }

    // Renders one host sample for up to `channels` voices. `in` carries the
    // per-voice pitch/gate; `cutoffCv`/`resonanceCv` are added to the
    // parameters (cutoff: octaves per volt; resonance: 0..1 offset).
    void process(const std::array<Input, voices> &in, int channels, const Parameters &p,
                 const FilterCalibration &cal, double host, float cutoffCv, float resonanceCv,
                 std::array<float, voices> &out, std::array<float, voices> &lpOut,
                 std::array<float, voices> &bpOut) {
        if (!std::isfinite(host) || host < 1000)
            host = 48000;
        int os = std::clamp(p.oversample, 1, 4);
        double fs4 = host * os;
        int count = std::clamp(channels, 0, voices);
        bool ad = p.envMode == 0;
        for (int c = 0; c < count; ++c) {
            Voice &v = state[c];
            if (v.first) {
                v.noise ^= 0x51A6B70Cu * uint32_t(c + 1);
                v.first = false;
            }
            Input input = in[c];
            if (!std::isfinite(input.pitch))
                input.pitch = 0;
            if (!std::isfinite(input.gate))
                input.gate = 0;
            bool gate = input.gate > .1f;
            // A gate edge kicks the filter, the way a note transient excites
            // a self-oscillating analog filter into its squeal, and clears
            // the decimator histories so the note starts from a clean ramp
            // instead of the tail of whatever the voice held before.
            if (gate && !v.gateWasOn) {
                v.bp += cal.kickAmplitude;
                v.decim.reset();
                v.lpDecim.reset();
                v.bpDecim.reset();
            }
            v.gateWasOn = gate;
            v.env1.step(gate, !ad, p.env1Attack, p.env1Decay, p.env1Sustain, p.env1Release, host);
            v.env2.step(gate, !ad, p.env2Attack, p.env2Decay, p.env2Sustain, p.env2Release, host);

            // VCO frequencies. Frequencies are clamped to 0.48 * host rate;
            // This is a digital rendering limit, not a property of analog VCOs.
            float f1 = 261.6255653005986f * std::exp2(std::clamp(input.pitch, -12.f, 6.5f));
            f1 = std::min(f1, float(.48 * host));
            float f2 = f1 * std::exp2(std::clamp(p.detune, -12.f, 12.f) / 12.f);
            f2 = std::min(f2, float(.48 * host));
            double inc1 = f1 / fs4, inc2 = f2 / fs4;

            // Filter coefficient and drive (control rate).
            float cutoffCvSafe = std::isfinite(cutoffCv) ? cutoffCv : 0;
            float resonanceCvSafe = std::isfinite(resonanceCv) ? resonanceCv : 0;
            float cutoff = std::clamp(p.cutoff * std::exp2(cutoffCvSafe), 1.f, 24000.f);
            float res = std::clamp(std::clamp(p.resonance, 0.f, 1.f) + resonanceCvSafe, 0.f, 1.f);
            // Damping k = 1/Q. At the very top of the pot k goes slightly
            // negative (the published "unstable" end of the travel). Computed
            // before g: the stability bound below needs it.
            float k = 2.f * std::pow(std::max(0.f, 1.f - res), cal.resonanceExponent) -
                      cal.selfOscAmount * std::pow(res, cal.selfOscSharpness);
            float fcEff = cutoff * std::exp2(2.f * v.env1.level * p.filterEnv);
            fcEff = std::min(fcEff, float(cal.maxCutoffRatio * fs4));
            double g = 2. * std::sin(dspemu::rate::pi * fcEff / fs4);
            // Stability bound for this sequential SVF update: the linear map
            // is stable iff g < sqrt(k^2 + 4) - k, and the saturated
            // Nyquist limit cycle bifurcates in at exactly the same g, so a
            // margin below the bound keeps both regimes safe even when
            // maxCutoffRatio is raised. At the default 0.1 ratio cap,
            // g <= 0.618 and the bound never binds.
            double gBound = (std::sqrt(double(k) * k + 4.) - k) * gStabilityMargin;
            g = std::min(g, gBound);
            float inGain = 1.f + cal.cutoffDriveScale * std::clamp(p.cutoffDrive, 0.f, 1.f) *
                                       std::pow(std::clamp(fcEff / 20000.f, 0.f, 1.f),
                                                cal.cutoffDrivePower);
            inGain *= 1.f + cal.resDriveAmount * res;

            float pw1 = std::clamp(p.osc1Pw, .05f, .95f);
            float pw2 = std::clamp(p.osc2Pw, .05f, .95f);
            // Never narrower than 1.5 sub-sample steps, so a pulse edge stays
            // representable.
            pw1 = std::max(pw1, float(1.5 * inc1));
            pw2 = std::max(pw2, float(1.5 * inc2));

            for (int s = 0; s < os; ++s) {
                // VCO1. The saw's single edge and the pulse's rising edge both
                // live at the wrap (pulseValue is -1 just below 1 and +1 just
                // above 0), so the wrap transition is +2 for pulse and -2 for
                // saw; the pulse's falling edge at pw is -2. A step can carry
                // both edges (wrap plus pw crossing), and scheduleEdges queues
                // both at their fractional positions.
                double p1 = v.phase1;
                v.phase1 += inc1;
                bool w1 = v.phase1 >= 1.0;
                if (w1)
                    v.phase1 -= 1.0;
                scheduleEdges(v.blep1, p1, inc1, 0.0, p.osc1Wave == 1, pw1, inc1);
                float osc1 = p.osc1Wave == 1 ? v.blep1.read(pulseValue(v.phase1, pw1))
                                             : v.blep1.read(float(2.0 * v.phase1 - 1.0));

                // VCO2, with hard sync to VCO1. The master wraps at fraction
                // t = (1-p1)/inc1 of this sub-sample; the slave evolves freely
                // up to that event (scheduling its own wrap/pulse edges on the
                // way), jumps to phase 0 there (a BLEP-corrected transition at
                // fraction t), and then advances only the remainder of the
                // sub-sample.
                double p2 = v.phase2;
                if (w1 && p.sync) {
                    double t = (1.0 - p1) / inc1;
                    double pre = t * inc2;
                    scheduleEdges(v.blep2, p2, pre, 0.0, p.osc2Wave == 1, pw2, inc2);
                    double pe = p2 + pre;
                    if (pe >= 1.0)
                        pe -= 1.0;  // the slave wrapped before the sync event
                    float delta = p.osc2Wave == 1
                                      ? (pulseValue(0.0, pw2) - pulseValue(pe, pw2))
                                      : float(-2.0 * pe);
                    if (delta != 0)
                        v.blep2.transition(delta, float(t));
                    double rem = (1.0 - t) * inc2;
                    scheduleEdges(v.blep2, 0.0, rem, t, p.osc2Wave == 1, pw2, inc2);
                    v.phase2 = rem;
                } else {
                    v.phase2 += inc2;
                    scheduleEdges(v.blep2, p2, inc2, 0.0, p.osc2Wave == 1, pw2, inc2);
                }
                if (v.phase2 >= 1.0)
                    v.phase2 -= 1.0;
                float osc2 = p.osc2Wave == 1 ? v.blep2.read(pulseValue(v.phase2, pw2))
                                             : v.blep2.read(float(2.0 * v.phase2 - 1.0));

                float mix = p.osc1Level * osc1 + p.osc2Level * osc2 +
                            p.noiseLevel * detail::noiseValue(v.noise) +
                            p.extLevel * (std::isfinite(input.external) ? input.external : 0.f);

                // Two-pole state-variable filter with saturating integrators
                // inside the feedback loop. The noise floor enters through
                // the resonator input, so it is DC-blocked and cannot drift
                // the states; above the self-oscillation threshold the
                // slightly negative damping turns it into the limit cycle.
                // Van der Pol damping: k grows with the square of the state
                // swing. In the linear regime (small states) this is exactly
                // k; when the self-oscillation loop gain exceeds one, the
                // amplitude settles where the extra damping balances it --
                // a stable, loud limit cycle at the filter's own frequency.
                // tanh(z)/z is 1 at z = 0, so the small-signal integrator
                // gain stays exactly g; the saturation only slews the rails.
                float kEff = k + cal.dampingCompression * (v.bp * v.bp + v.lp * v.lp);
                float x = std::tanh(inGain * mix * cal.inputDriveScale);
                float h = x - kEff * v.bp - v.lp;
                if (res >= cal.selfOscThreshold)
                    h += cal.selfOscSeed * detail::noiseValue(v.noise);
                v.bp = std::clamp(
                    v.bp + float(g) * std::tanh(h * cal.integ1Saturation) / cal.integ1Saturation,
                    -cal.stateClip, cal.stateClip);
                v.lp = std::clamp(
                    v.lp + float(g) * std::tanh(v.bp * cal.integ2Saturation) / cal.integ2Saturation,
                    -cal.stateClip, cal.stateClip);
                v.bp = detail::flushDenormal(v.bp);
                v.lp = detail::flushDenormal(v.lp);

                float tap = p.filterTap == 1 ? v.bp : v.lp;
                // Overdrive: dry/wet mix into a level-compensated tanh, so
                // drive = 0 is transparent and drive = 1 saturates hard.
                float wet = std::tanh(tap * (1.f + cal.overdriveGain * p.drive)) /
                            std::tanh(1.f + cal.overdriveGain * p.drive);
                float sample = detail::flushDenormal((tap * (1.f - p.drive) + p.drive * wet) * p.level);
                v.hist[s] = sample;
                // Every internal sample enters the decimators (main output
                // and both calibration taps); they are read once per base
                // sample below, after all `os` pushes of the period.
                v.decim.push(sample);
                v.lpDecim.push(v.lp);
                v.bpDecim.push(v.bp);
            }
            float y;
            if (os == 1) {
                // 1x is the identity path: the internal rate is the host
                // rate and nothing needs decimating.
                y = v.hist[0];
                lpOut[c] = v.lp;
                bpOut[c] = v.bp;
            } else {
                y = v.decim.read(decimFraction, 1.0 / os);
                lpOut[c] = v.lpDecim.read(decimFraction, 1.0 / os);
                bpOut[c] = v.bpDecim.read(decimFraction, 1.0 / os);
            }
            y = detail::flushDenormal(y);
            // Flush after the VCA too: two normal tiny values can multiply to a subnormal.
            out[c] = detail::flushDenormal(y * v.env2.level);
        }
        // Retired voices release: their envelopes run to zero and their filter
        // states decay, so re-expanding the polyphony later never finds a stale
        // held envelope or a frozen filter state (or a denormal) in an
        // unprocessed voice. Each voice keeps its own state, so voices trigger
        // independently. This also covers the count == 0 case.
        for (int c = count; c < voices; ++c) {
            out[c] = 0;
            lpOut[c] = 0;
            bpOut[c] = 0;
        }
        if (count < voices) {
            float retire = float(1. - std::exp(-1. / (retiredVoiceDecaySeconds * host)));
            for (int c = count; c < voices; ++c) {
                Voice &v = state[c];
                v.env1.step(false, !ad, p.env1Attack, p.env1Decay, p.env1Sustain, p.env1Release,
                            host);
                v.env2.step(false, !ad, p.env2Attack, p.env2Decay, p.env2Sustain, p.env2Release,
                            host);
                v.lp -= retire * v.lp;
                v.bp -= retire * v.bp;
                v.lp = detail::flushDenormal(v.lp);
                v.bp = detail::flushDenormal(v.bp);
                v.gateWasOn = false;  // a later gate edge kicks again
                v.hist = {};
            }
        }
        deviceFrames++;
    }

    // Test hook: reports whether any internal state holds a denormal.
    bool hasDenormals() const {
        for (const auto &v : state) {
            if (std::fpclassify(v.lp) == FP_SUBNORMAL || std::fpclassify(v.bp) == FP_SUBNORMAL ||
                std::fpclassify(v.env1.level) == FP_SUBNORMAL ||
                std::fpclassify(v.env2.level) == FP_SUBNORMAL)
                return true;
            for (float h : v.hist)
                if (std::fpclassify(h) == FP_SUBNORMAL)
                    return true;
            // Everything pushed into the decimators is flushDenormal'd, so
            // their histories cannot hold denormals; the check is kept for
            // completeness in case that invariant ever changes.
            for (const auto &d : {&v.decim, &v.lpDecim, &v.bpDecim})
                for (float h : d->history)
                    if (std::fpclassify(h) == FP_SUBNORMAL)
                        return true;
        }
        return false;
    }

  private:
    // Time constant of the filter-state decay applied to retired voices.
    static constexpr double retiredVoiceDecaySeconds = 0.010;
    // Fractional read position of the base-rate decimator read, in internal
    // samples behind the newest pushed sample: the kernel peak sits at
    // taps/2 - 1 = 31 samples, so 1023/1024 reads the signal ~30 internal
    // samples back (minimum reachable latency, one phase step inside the
    // window). The read lands ~1 internal sample before a base-sample
    // boundary for every oversampling factor, giving ~15 base samples of
    // latency at 2x and ~7 at 4x.
    static constexpr double decimFraction = 1023.0 / 1024.0;
    // Margin below the linear/nonlinear stability boundary g = sqrt(k^2+4)-k.
    static constexpr double gStabilityMargin = 0.9;
    static float pulseValue(double phase, float pw) { return phase < pw ? 1.f : -1.f; }
    // Schedules the BLEP corrections for every edge the phase crosses while it
    // advances `dist` cycles from `phase` (in [0, 1)) starting at sub-sample
    // fraction `start`. The saw's single edge and the pulse's rising edge both
    // live at the wrap (pulseValue jumps -1 -> +1 there, so its polarity is
    // +2 while the saw's is -2); the pulse's falling edge lives at pw (-2).
    // A wrapped step can contain both the wrap and the pw crossing, and both
    // corrections queue into the same BLEP hold.
    static void scheduleEdges(dspemu::rate::BlepHold &blep, double phase, double dist,
                              double start, bool pulse, float pw, double inc) {
        if (!(dist > 0))
            return;
        double end = phase + dist;
        if (end >= 1.0) {
            double f = start + (1.0 - phase) / inc;
            if (f <= 1.0)
                blep.transition(pulse ? 2.f : -2.f, float(f));
        }
        if (pulse && phase < pw && pw <= end) {
            double f = start + (double(pw) - phase) / inc;
            if (f > 0 && f <= 1.0)
                blep.transition(-2.f, float(f));
        }
    }
};

} // namespace dspemu::pvx
