# Spoon Bender (formerly ESPish): effects module spec

Status: **v1 implemented** (2.1.0, renamed from ESPish; §11 lists the post-release fixes). This document is the design record. The
user-facing manual is [README.md](README.md).

The draft was written on 2026-10-14 from proposals by GPT, Google Argon and
Claude. Hardware claims were checked against MAME at commit
[`016b077`](https://github.com/mamedev/mame/tree/016b077915e5e12349e5d590fbbe47595717d27b).
The review (GPT and Claude; Argon's review errored out) settled the open
questions in §10. Items marked *assumed*, *inferred* or *unverified* still
want a primary source.

## 1. Purpose

A stereo effects module for the OTIS → Vastish chain. A transformation stage
(echo) feeds a space stage (diffusion and decay). It has an optional EPS-era
fixed-point character and a way to hand what it just played back to OTIS as a
sample.

It is **not chip emulation**. It runs no Ensoniq microcode and doesn't model
the ES5510 instruction set. It borrows the chip's number formats and rounding.
See §3. The name (ESPish until 1.1.0, now Spoon Bender) carries that condition: the panel and plugin
description say so.

Target sounds (from the GPT proposal), and whether v1 covers them:

| Sound | v1 |
|---|---|
| Short ensemble into a modest room | **Deferred** with ENSEMBLE (§2). Short echo with MOD gets part of the way. |
| Rhythmic echoes dissolving into reverberation | Yes |
| Tuned glassy repeats opening into a space | Approximated with the frequency shifter (§5.3); a true resonator bank is later |
| Reverse grains into a dark hall | Later (granular is out of v1 scope) |

## 2. Scope

**v1 (shipped):** stereo in/out; ECHO with a frequency shifter in its feedback
loop; space stage; serial routing; bounded feedback; CLEAN and ESP engines;
retroactive capture to OTIS.

**Deferred by review:** ENSEMBLE (multi-tap modulated delay), so that ECHO,
SHIFT and SPACE ship first.

**v1.1:** ENSEMBLE as a second transform mode; parallel routing; a bounded
space → transform return path.

**Later, if they earn a place:** granular delay, resonator bank, reverse. Rack
already has strong granular and resonator modules (the Mutable Instruments
ports). Ours should only exist if they do something those cannot, such as
working on OTIS's sample memory or sharing the ESP character.

## 3. What the hardware evidence supports

### 3.1 Why not emulate the chip

The EPS-16 Plus's effects ran on the "Ensoniq Signal Processor (ESP) chip"
([Music Technology review, 1990](https://www.muzines.co.uk/articles/ensoniq-eps16-plus/775)).
MAME identifies this chip as the ES5510, a programmable DSP. The review lists
13 effects (reverbs, delays, chorus/flange/phaser/rotary/distortion/wah
combinations), and these were Ensoniq microcode supplied by the instrument's
software. A faithful chip emulation would therefore need one of two things:

- Ensoniq's copyrighted programs, which this repo will not distribute (OTIS
  needs no firmware either).
- New microcode written for a 1990s DSP.

Neither belongs in v1.

MAME also cannot serve as an EPS-16 Plus reference for the ESP. Its `eps16p`
machine reuses the EPS configuration, whose memory map has no ESP registers,
and the machine is marked `MACHINE_NOT_WORKING`
([`esq5505.cpp`](https://github.com/mamedev/mame/blob/016b077915e5e12349e5d590fbbe47595717d27b/src/mame/ensoniq/esq5505.cpp#L607-L617),
[line 1293](https://github.com/mamedev/mame/blob/016b077915e5e12349e5d590fbbe47595717d27b/src/mame/ensoniq/esq5505.cpp#L1293)).
MAME's ES5510 core is exercised through the VFX/SD-1 drivers (rated imperfect
sound) and some arcade boards.

### 3.2 Facts taken from MAME's ES5510 core

Source: [`es5510.cpp`](https://github.com/mamedev/mame/blob/016b077915e5e12349e5d590fbbe47595717d27b/src/devices/cpu/es5510/es5510.cpp)
and [`es5510.h`](https://github.com/mamedev/mame/blob/016b077915e5e12349e5d590fbbe47595717d27b/src/devices/cpu/es5510/es5510.h),
BSD-3-Clause (Christian Brunschen).

| Property | MAME | Status |
|---|---|---|
| Data path | 24-bit registers (192 GPRs) | From core (`es5510.cpp` L590) |
| Multiply-accumulate | 48-bit accumulator. Write-back takes bits 24–47 of the product shifted by `mulshift`, saturating if the 48-bit value overflows. `mulshift` resets to 2; SIGREG bit 22 selects 1. With 1, write-back is a Q23 × Q23 → Q23 multiply. | From core (`es5510.h` L156, `es5510.cpp` ~L906–913) |
| ALU ADD/SUB | **Saturate** to 0x7FFFFF / 0x800000 | From core (`saturate()`, L121) |
| ALU ADDU/SUBU | **Wrap** (no saturation) | From core |
| SUB of 0x800000 | Negating −2²³ wraps to itself, so SUB of it acts as ADD | Quirk in core; may or may not match hardware |
| Delay memory | 16-bit words | From core |
| 24 → 16-bit write to delay memory | Drops the low 8 bits, rounding **toward −∞** by default | Default in core (L29). Round-toward-zero exists but is disabled, and the authors say it "may not be what the hardware does". |
| Program length | 160 instruction slots | From core (`es5510.h` L119) |
| Delay memory size | 1M words placeholder; a comment says most games use 128 KB | **Unverified** in MAME (`es5510.h` L17–18) |
| Sample rate | VFX/SD-1 pump at 10 MHz / (16 × 21) ≈ 29,761.9 Hz (`esq5505.cpp` L795). This equals OTIS's default chip rate (10 MHz / 336). | For the EPS-16 Plus, **inferred** (see the reasoning below the table) |

How the EPS-16 Plus rate is inferred:

1. The 1990 review says the "44 kHz versions" of the effects cut polyphony
   from 20 voices to 13.
2. OTIS's output rate is clock / (16 × active oscillators)
   ([MAME `es5506.cpp`](https://github.com/mamedev/mame/blob/016b077915e5e12349e5d590fbbe47595717d27b/src/devices/sound/es5506.cpp#L1157)).
3. 21 and 14 oscillators give 29.76 kHz and 44.64 kHz (OTIS's 10 MHz / 224).
   That is one more oscillator than the review's 20 and 13 voices, which fits
   if one oscillator is reserved.
4. So the ESP appears to run at OTIS's output rate.

The reserved-oscillator reading and the conclusion are ours. Confirm them from
a service manual if possible.

### 3.3 Corrections to the Argon draft

- `wrap24` left-shifts a possibly negative signed integer, which is undefined
  behaviour before C++20. Do shifts on unsigned values.
- Argon's draft had a global "WRAP" overflow mode and attributed it to the
  K2000. That mixes instruments. In MAME, ADD/SUB saturate and only the
  separate ADDU/SUBU opcodes wrap. Spoon Bender's algorithms need no wrapping
  arithmetic, so the WRAP mode was dropped. The parity test still checks ADDU
  wraps.
- Delay-memory writes: Argon rounds toward zero, but MAME's default rounds
  toward −∞. Spoon Bender follows MAME.
- `sat24` and the 48-bit accumulator idea are consistent with MAME and kept.

## 4. Signal flow (as built)

```
         processed path: CLEAN at host rate, ESP at internal rate behind the converters
IN L ─┬─► ECHO ─► echo mix ─► SPACE ─► processed part of the mixes ─► DC block (5 Hz) ─► (+) ─► MIX with dry ─► OUT L/R
IN R ─┘    ▲  │                                                                         ▲                  │
(R → L)    └──┘ feedback: 40 Hz HP → TONE → guard LPF → shifter → ×FB → limiter         │                  └─► capture ring (mono) ─► OTIS (§7)
      └──── direct term: input × (1 − echo mix) × (1 − space mix), at host rate ────────┘
```

The direct (dry) parts of all three mixes stay at the host rate. In ESP they
used to pass the converters, whose latency combed against the global MIX's dry
path (§11).

- The output is stereo throughout. A mono source (OTIS) into IN L feeds both
  channels.
- Echo mix, space mix and a global MIX are separate, so the space can sit after
  a fully wet echo or after a lightly treated signal.
- v1 routing is serial.

## 5. Stages

### 5.1 ECHO

- Stereo delay with TIME from 1 ms to 2 s, exponential. In ESP with the 64K
  budget, the maximum is shorter (§6.2).
- CLOCK input: when patched, TIME selects 1/4, 1/3, 1/2, 2/3, 3/4, 1, 3/2, 2, 3
  or 4 × the measured clock period. The draft's "1/16 … 4 bars" assumed a bar
  length the module can't know.
- FEEDBACK runs from 0 to 110%. Above 100% the in-loop limiter (§5.4) holds
  the level, so repeats sustain without running away.
- Feedback path order: 40 Hz high-pass → TONE → guard low-pass → frequency
  shifter (if LOOP) → × feedback → soft limiter → delay write.
  - TONE is gain-neutral or cutting in both directions: left blends toward a
    low-pass, right subtracts lows. An earlier boost curve pushed the loop gain
    above 1.
  - The guard low-pass (2-pole Butterworth at min(18 kHz, 0.4 × rate)) stops
    the shifter folding content near Nyquist back down. Without it the loop
    gained power each pass at maximum shift.
- SPREAD: parallel stereo ↔ ping-pong.
- Time changes glide over 50 ms (tape-style pitch bend). The draft's
  crossfaded-jump option was dropped.

### 5.2 ENSEMBLE: deferred to v1.1

Multi-tap modulated delay (3 voices per side) with RATE, DEPTH and WIDTH.

### 5.3 Frequency shifter (in the echo feedback loop)

- Single-sideband shift: a Hilbert transformer (two parallel all-pass chains
  with 90° phase difference, Olli Niemitalo's coefficients) and a quadrature
  oscillator. The oscillator is a rotating phasor, renormalised every 256
  samples.
- SHIFT is bipolar and exactly 0 at centre (deadband of ±2% of travel). It is
  exponential from 0.1 Hz to the range limit, with CV.
- The review kept the wide range and added a fine one, chosen by switch:
  - **FINE**: ±0.1–20 Hz, for slow beating and drifting repeats.
  - **WIDE**: ±0.1 Hz–2 kHz.
- Each pass through the loop adds the offset again, so repeats climb or fall by
  a fixed number of Hz. The result is inharmonic, not an octave shimmer.
- Position: LOOP (default; repeats accumulate the shift) or POST (shift the
  echo output once).
- Requirement: unwanted-sideband suppression ≥ 40 dB from 100 Hz to 10 kHz at
  48 kHz. Measured: ≥ 44.5 dB.

### 5.4 Bounded feedback

The echo loop passes through a 40 Hz high-pass (which also blocks DC), then a
soft limiter: 1.2·tanh(x / 1.2), hard-clamped at ±2.0 internal units.

Requirement: with every control at its maximum and noise input, the output
stays finite and bounded for 60 s, then decays to silence once feedback is
lowered. Measured peaks: 1.20 (CLEAN) and 0.81 (ESP). Release tails are
< 1e-6 (CLEAN) and < 1e-3 (ESP).

### 5.5 Space

A feedback-delay-network reverb, built in this order:

1. Predelay.
2. Four series all-pass diffusers per channel, with gain 0.75 × DIFFUSION.
3. An 8-line FDN with a Householder matrix. Base lengths are 31.7–73.1 ms,
   scaled by SIZE from 0.25× to 2×.
4. Output taps with sign patterns per channel.

Separate controls, never merged into one "character" knob:

| Control | Meaning |
|---|---|
| PREDELAY | 0–250 ms |
| SIZE | Delay-line lengths (room ↔ hall), gliding over 200 ms |
| DECAY | RT60 0.2–30 s (per-line gain), plus FREEZE |
| DIFFUSION | Input all-pass gain: discrete slapback ↔ smooth |
| DAMPING | One-pole high-frequency loss per pass |
| MODULATION | Up to 1.2 ms of modulation on alternate lines (chorus in the tail; reduces metallic ringing) |

FREEZE sets line gain to 1 and turns damping and modulation off. Reads become
integer, so no interpolation loss occurs, and space input and diffuser
injection are muted. In ESP the frozen loop is also not re-quantized (§6.2).

## 6. Engines

### 6.1 CLEAN

32-bit float at the host rate. No deliberate degradation. Tiny values are
flushed to zero at memory writes, so tails settle to exact zero without
denormals.

### 6.2 ESP

The same echo and space code, compiled with a different number policy and run
at a reduced internal rate. This models the period's *number formats and
memory*, not Ensoniq's programs.

- **Internal rate:** 29,761.9 Hz by default (10 MHz / 336, the 20-voice rate).
  The alternatives are 44,642.9 Hz (10 MHz / 224, the review's "44 kHz"
  effects) and 78,125 Hz (10 MHz / 128, not a period rate). The choice is in
  the menu and saved with the patch.
  - The rates are **provisional** (§3.2).
  - Host ↔ internal conversion uses a copy of OTIS's 64-tap, 1024-phase
    windowed-sinc converter.
- **Node quantization:** every summing node and gain output is floored to
  24-bit Q23 and saturated to [−1, 1 − 2⁻²³]. That covers all-pass sums, FDN
  matrix outputs, the feedback product and the limiter output.
- **Memory quantization:** every delay-memory write is floored to 16 bits,
  toward −∞ (MAME's default), and saturated.
- **Coefficients:** quantized to Q23.
- **Float in between:** filters, the Hilbert all-passes, the quadrature
  oscillator and the converters run in float between quantization points. The
  floor-and-clamp quantizers equal the integer path (`sat24`, `toDelayWord`)
  bit for bit; the test checks 2M inputs.
- **DC limit cycle:** rounding toward −∞ leaves recirculating tails on a
  −1 LSB (about −90 dBFS) DC limit cycle. The hardware would pass that on.
  Spoon Bender removes it with a 5 Hz DC blocker on the wet output, which also made
  the RT60 measurement meaningful.
- **Delay-memory budget:** 64K words (default) or UNLIMITED. The budget covers
  the **echo lines only**: 32K words each, so the maximum echo is 1.10 s at
  29.76 kHz, 0.73 s at 44.64 kHz and 0.42 s at 78.13 kHz. The space's lines
  are not counted.
  - The 64K figure is a deliberate creative limit, period-plausible but **not
    a verified EPS-16 Plus figure** (§3.2).
- **Dry path:** only the processed signal runs in ESP. The direct terms of the
  mixes are added at the host rate (§4), so ESP never band-limits, quantizes
  or delays the dry signal. The converters add 1.69 ms of latency to the
  processed signal at 29.76 kHz, 1.32 ms at 44.64 kHz and 1.01 ms at
  78.13 kHz (measured, host at 48 kHz). That is 31 samples at each side's
  rate. Echo spacing inside the loop is unaffected.
- **FREEZE:** the frozen FDN recirculates without node or memory quantization.
  The content was quantized on the way in.
  - In a lossless loop, rounding error accumulates instead of decaying. Quiet
    tails grew by 7.4 dB in 30 s when the loop kept re-quantizing (§11).
  - The output tap is still quantized.
- The 160-instruction limit is not modelled as a cycle budget.

Integer reference primitives (`src/engine/FixedPoint.hpp`):

```cpp
// 24-bit two's complement held in int32_t. Shifts on unsigned values only.
inline int32_t sat24(int64_t x) {                    // as MAME's saturate()/MAC write-back
    return x > 0x7FFFFF ? 0x7FFFFF : x < -0x800000 ? -0x800000 : int32_t(x);
}
inline int32_t sext24(uint32_t x) {                  // 24-bit pattern -> signed value
    x &= 0xFFFFFFu; return int32_t(x ^ 0x800000u) - 0x800000;
}
inline int16_t toDelayWord(int32_t v24) {            // MAME default: toward -infinity
    return int16_t(v24 >> 8);                        // arithmetic shift (defined in C++20; GCC/Clang/MSVC earlier too)
}
inline int32_t fromDelayWord(int16_t w) { return int32_t(w) * 256; }
inline int32_t mul24(int32_t a, int32_t b) {         // Q23 x Q23 -> Q23; equals MAME's MAC write-back with mulshift 1
    return sat24((int64_t(a) * b) >> 23);
}
```

Switching engine or ESP rate clears the effect state. Rate changes reallocate
and happen off the audio thread under a mutex. `process()` only `try_lock`s
and outputs silence for the few frames a reconfiguration takes.

## 7. Capture to OTIS (retroactive)

OTIS can already record what you patch into it, but only from the moment you
press REC. Spoon Bender keeps the **recent past**:

- **Ring buffer:** two rolling rings hold the post-MIX output: a mono sum
  (L+R)/2, 16-bit, at the **host rate** in both engines, up to 20 s.
  - The draft captured ESP mode at the internal rate. Host rate is simpler and
    records what was heard, including the converter.
  - Captures are mono, matching OTIS's sample memory. A stereo capture for
    future consumers was considered and deferred.
- **Capture:** CAPTURE (button or trigger) freezes the last LENGTH seconds
  (1–20 s) and switches writing to the other ring. The audio thread doesn't
  allocate, copy or wait.
  - The UI thread copies the frozen ring and releases it.
  - The copy and a sample-rate change (which rebuilds the rings) are
    serialised by a mutex the audio thread never takes. A rate change discards
    an uncollected capture.
  - Each capture carries the rate it was recorded at, not the engine's current
    rate.
  - Only one capture can be pending at a time.
  - The next capture's history starts from the switch.
- **Share:** the copy is exposed through `vcv/shared/SampleShare.hpp` (version
  1) on the module's right-expander pointers, exactly as OTIS shares with
  Vastish.
  - The revision goes odd while the slot changes and even when ready, so it
    advances by 2 per capture.
  - `revision()` also collects a pending capture, so a reader sees it even
    before Spoon Bender's own widget has stepped.
- **OTIS reader (2.5.0; 2.5.1 for the new name):** the menu has **Import capture from Spoon Bender on the
  left** (one-shot) and **Follow Spoon Bender captures** (saved with the patch).
  - Rack layout: `[Spoon Bender][OTIS][Vastish]`. OTIS reads its left neighbour and
    still shares to its right.
  - Lookup uses the UI-owned `leftExpander.moduleId` and `Engine::getModule()`,
    as fixed in Vastish 2.2.2, not the engine-owned pointer. The neighbour is
    re-resolved at click time.
  - A copy whose revision changed mid-read is discarded. When following, it is
    retried on the next frame.

So: play through the effect, hear something good after the fact, and press
CAPTURE. The reverb tail or echo trail becomes an OTIS sample to REWORK, and
from there a Vastish oscillator.

## 8. Panel and I/O (32 HP)

| | |
|---|---|
| Inputs | IN L, IN R (normalled to L), CLOCK, CAPTURE trig, FREEZE gate, CV for TIME, FEEDBACK, SHIFT, DECAY, MIX. Polyphony is not used. |
| Outputs | OUT L, OUT R |
| Echo | TIME, FEEDBACK, TONE, SHIFT, SPREAD, ECHO MIX; FINE/WIDE and LOOP/POST switches |
| Space | PREDELAY, SIZE, DECAY, DIFFUSION, DAMPING, MOD, SPACE MIX, FREEZE latch |
| Global | ENGINE (CLEAN/ESP), MIX, CAPTURE, LENGTH |
| Menu | ESP rate, echo memory budget, capture status |
| Lights | LIMIT, FREEZE, CAPTURE pending |

`tools/make_panel.py` generates the panel SVG (outlined DejaVu Sans) and
`src/Layout.hpp` from one table, so artwork and component positions can't drift
apart.

## 9. Testing

`tests/engine_test.cpp` is portable, runs on all three platforms in CI, and
under ASan/UBSan on Linux:

| Check | Requirement | Measured |
|---|---|---|
| Arithmetic vs MAME (functions copied with attribution) | Bit-exact | 10M cases: ADD/SUB saturating (with the SUB quirk), ADDU wrapping, 24 → 16-bit write, MAC write-back at mulshift 1 |
| Float quantizers vs integer path | Bit-exact | 2M inputs |
| Shifter unwanted sideband, 100 Hz–10 kHz | ≥ 40 dB | ≥ 44.5 dB |
| Stability, maxed controls (60 s at 48 kHz; 10 s at 44.1 and 96 kHz) | Finite, bounded, decays after release | See §5.4 |
| RT60 (noise burst), set 1 s / 3 s | ±15% | CLEAN 0.90 / 2.69 s; ESP 0.93 / 2.79 s |
| FREEZE drift over 30 s, excitation 0.3 / 0.003 / 0.0003 | < 0.5 dB, both engines | ≤ 0.06 dB |
| Dry path, stage mixes at zero | Output equals input bit for bit at any MIX; direct term exact before the first repeat | Pass, both engines |
| ESP converter at 29.76 kHz | Passband ±0.5 dB; stopband and aliases < −40 dB | 1 kHz −0.003 dB; 20 kHz −89.5 dB; folded image −90.9 dB |
| Echo timing | CLEAN ±0.2 ms, ESP ±3 ms; budget enforced | Pass; 1.101 s limit |
| Silence | Exact zero, no denormals | Pass (CLEAN) |
| CPU, 48 kHz, one core | Target < 3% each; test fails above 10% | CLEAN ≈ 2.4–2.5%; ESP ≈ 3.4% (-O3) to 4.9% (-O2, busy machine). **ESP misses the 3% target.** |

Linux CI also runs:

- `tests/capture_race_test.cpp` under ThreadSanitizer. It uses three threads:
  - audio: try-lock, tick, trigger
  - reconfiguration: lock and re-prepare at alternating rates
  - UI: collect

  Each capture must carry a valid rate and fit its ring. Without the capture
  mutex, TSAN reports the race.

- `tests/module_test.cpp`, which drives the real module through Rack's API:
  - dry path and normalling
  - clock-synced echo, landing within 1 ms
  - shift display at the centre and both ranges
  - freeze gate
  - typed SHIFT values in both ranges, and typed TIME in seconds and as clock
    ratios
  - capture → share, covering revisions, rate and name, and a sample-rate
    change with a capture pending
  - reconfiguration at every ESP rate
  - JSON
- `vcv/otislab/tests/fx_link_test.cpp`: the real OTIS code imports and follows
  captures from a module created from the compiled Spoon Bender binary.

Not yet tested: a live Rack GUI/audio session, and the Windows and macOS
binaries inside Rack.

## 10. Review decisions

| Question | Decision |
|---|---|
| v1 scope | Defer ENSEMBLE; ship ECHO + SHIFT + SPACE first. |
| Shifter range | Keep ±2 kHz (WIDE). Add a precise centre (deadband) and a FINE ±20 Hz range. |
| ESP rates and memory budget | Keep them, explicitly labelled provisional (inferred / period-plausible, not verified). An EPS-16 Plus service manual or schematic would settle both. |
| Capture format | Stereo output; mono captures to OTIS for now. |
| Name | Keep ESPish, provided the description says explicitly that it is not chip emulation. Renamed to Spoon Bender in 1.1.0 (still not chip emulation; the ESP engine keeps its name). |

## 11. Post-release review (1.0.1)

GPT's testing of 1.0.0 found four problems. Each was reproduced, fixed and
given a test that fails on the old code.

| Finding | Cause | Fix |
|---|---|---|
| P1: capture copy not synchronised with sample-rate changes; the rate was read from the live engine | `collectCapture()` read the rings without a lock while `onSampleRateChange()` rebuilt them | A mutex serialises collection and rebuilding; the audio thread doesn't use it. The rate is stored with the frozen capture. Covered by a TSAN test. |
| P2: ESP comb filtering at partial MIX (−41.5 dB at 300 Hz, both stage mixes at 0 and MIX at 50%) | The stage mixes' dry parts went through the converters (1.69 ms), then met the undelayed global dry path | Direct terms are added at the host rate (§4); the dry path is bit-exact |
| P2: typing a SHIFT value set the knob position, so typing 1 gave +2 kHz | No inverse of the display mapping | `setDisplayValue` inverts the Hz curve for the current range; TIME accepts clock ratios when clocked |
| P2: quiet ESP frozen tails grew (+7.44 dB in 30 s at 0.0003 excitation) | Re-quantizing a lossless loop accumulates rounding noise | No re-quantization while frozen; tested at three excitation levels |
