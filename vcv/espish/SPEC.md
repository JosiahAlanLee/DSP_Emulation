# ESPish: effects module spec (draft for review)

Status: **specification only, no code yet.** Working name; open to change.
Written 2026-10-14 from proposals by GPT, Google Argon and Claude, with hardware
claims checked against MAME at commit
[`016b077`](https://github.com/mamedev/mame/tree/016b077915e5e12349e5d590fbbe47595717d27b).
Reviewers: please challenge anything marked *assumed* or *unverified*.

## 1. Purpose

A stereo effects module for the OTIS → Vastish chain: a transformation stage
(echo, ensemble) feeding a space stage (diffusion and decay), with an optional
EPS-era fixed-point character, and a way to hand what it just played back to OTIS
as a sample.

It is **not** an ES5510 emulator and runs no Ensoniq microcode. See §3.

Target sounds (from the GPT proposal), and whether v1 covers them:

| Sound | v1 |
|---|---|
| Short ensemble into a modest room | Yes |
| Rhythmic echoes dissolving into reverberation | Yes |
| Tuned glassy repeats opening into a space | Approximated with the frequency shifter (§5.3); a true resonator bank is later |
| Reverse grains into a dark hall | Later (granular is out of v1 scope) |

## 2. Scope

**v1:** stereo in/out; transform stage with ECHO and ENSEMBLE modes; frequency
shifter inside the echo feedback loop; space stage; serial routing; bounded
feedback; CLEAN and ESP CHARACTER engines; retroactive capture to OTIS.

**v1.1:** parallel routing; a bounded space → transform return path.

**Later, if they earn a place:** granular delay, resonator bank, reverse. Rack
already has strong granular and resonator modules (the Mutable Instruments
ports); ours should only exist if they do something those cannot, such as
working on OTIS's sample memory or sharing the ESP character.

## 3. What the hardware evidence supports

### 3.1 Why not emulate the chip

The EPS-16 Plus's effects ran on the "Ensoniq Signal Processor (ESP) chip"
([Music Technology review, 1990](https://www.muzines.co.uk/articles/ensoniq-eps16-plus/775)),
which MAME identifies as the ES5510, a programmable DSP. The review lists 13
effects (reverbs, delays, chorus/flange/phaser/rotary/distortion/wah
combinations). The effects themselves were Ensoniq microcode supplied by the instrument's software.
A faithful chip emulation therefore needs either Ensoniq's copyrighted programs,
which this repo will not distribute (OTIS needs no firmware either), or new
microcode written for a 1990s DSP. Neither belongs in v1.

MAME also cannot serve as an EPS-16 Plus reference for the ESP: its `eps16p`
machine reuses the EPS configuration, whose memory map has no ESP registers, and
the machine is marked `MACHINE_NOT_WORKING`
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
| Multiply-accumulate | 48-bit accumulator; reading it saturates to 24 bits on overflow | From core (`es5510.h` L156, `es5510.cpp` ~L906–913) |
| ALU add/subtract overflow | **Saturates** to 0x7FFFFF / 0x800000 | From core (`saturate()`, L121) |
| Delay memory | 16-bit words | From core |
| 24 → 16-bit write to delay memory | Drops the low 8 bits, rounding **toward −∞** by default | Default in core (L29); round-toward-zero exists but is disabled and the authors say it "may not be what the hardware does" |
| Program length | 160 instruction slots | From core (`es5510.h` L119) |
| Delay memory size | 1M words placeholder; comment says most games use 128 KB | **Unverified** in MAME (`es5510.h` L17–18) |
| Sample rate | VFX/SD-1 pump at 10 MHz / (16 × 21) ≈ 29,761.9 Hz (`esq5505.cpp` L795), equal to OTIS's default chip rate (10 MHz / 336) | For the EPS-16 Plus, **inferred**: the 1990 review says the "44 kHz versions" of the effects cut polyphony from 20 voices to 13. OTIS's output rate is clock / (16 × active oscillators) ([MAME `es5506.cpp`](https://github.com/mamedev/mame/blob/016b077915e5e12349e5d590fbbe47595717d27b/src/devices/sound/es5506.cpp#L1157)); 21 and 14 oscillators give 29.76 kHz and 44.64 kHz (OTIS's 10 MHz / 224), one more than the review's 20 and 13 voices, consistent if one oscillator is reserved. So the ESP appears to run at OTIS's output rate. The reserved-oscillator reading and the conclusion are ours; confirm from a service manual if possible |

### 3.3 Corrections to the Argon draft

- `wrap24` left-shifts a possibly negative signed integer: undefined behaviour
  before C++20. Do shifts on unsigned values.
- The ES5510 ALU in MAME **saturates**; nothing in the core supports a "WRAP"
  overflow mode, and attributing one to the K2000 mixes different instruments.
  Dropped.
- Delay-memory writes: Argon rounds toward zero; MAME's default rounds toward
  −∞. Default to MAME's behaviour; keep round-toward-zero as a hidden test option
  only.
- `sat24` and the 48-bit accumulator idea are consistent with MAME and kept.

## 4. Signal flow

```
IN L ─┬─► [input trim] ─► TRANSFORM (ECHO | ENSEMBLE) ─► SPACE ─► [mix] ─► OUT L/R
IN R ─┘   (R normalled to L)        ▲   │                            ▲
                                    │   └─ freq shifter + limiter ───┘ (echo feedback, §5.3)
                                    └──── dry/wet per stage ─────────┘
                        rolling capture buffer (post-mix) ─► CAPTURE ─► OTIS (§7)
```

- Stereo throughout. A mono source (OTIS) into IN L feeds both channels.
- Each stage has its own dry/wet (so the space can sit after a fully wet echo or
  a lightly treated signal), plus a global MIX.
- v1 routing is serial. v1.1 adds PARALLEL (both stages fed from the input) and
  a bounded space → transform return.

## 5. Stages

### 5.1 Transform: ECHO

- Stereo delay, TIME 1 ms–2 s (CLEAN). In ESP CHARACTER the maximum is set by the
  delay-memory budget (§6.2).
- CLOCK input: when patched, TIME selects a division/multiple of the incoming
  clock period (1/16 … 4 bars) instead of milliseconds.
- FEEDBACK 0–110%: above 100% the in-loop limiter (§5.4) holds it, so it
  self-sustains without running away.
- Feedback path order: high-pass (fixed ~40 Hz) → TONE (tilt / low-pass) →
  frequency shifter (§5.3) → limiter → delay.
- SPREAD: ping-pong ↔ parallel stereo.
- Time changes glide (tape-style pitch bend) rather than click; a menu option
  switches to crossfaded jumps.

### 5.2 Transform: ENSEMBLE

- Multi-tap modulated delay (3 voices per side), RATE, DEPTH, and a WIDTH that
  spreads voices across the stereo field. This is the "everyday keyboard
  treatment".

### 5.3 Frequency shifter (in the echo feedback loop)

- Single-sideband shift via a Hilbert transformer (two parallel 90°-phase-
  difference all-pass chains) and a quadrature oscillator.
- SHIFT: bipolar, 0 at centre, roughly ±0.1 Hz to ±2 kHz on an exponential
  curve, with CV. Each pass through the loop adds the offset again, so repeats
  climb or fall by a fixed number of Hz. Inharmonic, not an octave shimmer.
- Position: IN LOOP (default; repeats accumulate the shift) or POST (shift the
  echo output once).
- Requirement: unwanted-sideband suppression ≥ 40 dB from 100 Hz to 10 kHz at
  48 kHz (measured in tests, §9).

### 5.4 Bounded feedback

Every feedback path (echo loop, and the v1.1 return) passes through: DC blocker →
soft saturator (smooth, odd-symmetric) → hard ceiling at ±2.0 (internal units).
Requirement: with every control at its maximum and a full-scale input burst, the
output stays finite and below the ceiling for 60 s, then decays to silence once
feedback is lowered below 100%.

### 5.5 Space

A feedback-delay-network reverb: input diffusion (series all-passes) → 8-line
FDN with a Householder mixing matrix → output taps per channel.

Separate controls, never merged into one "character" knob (both GPT and Claude
argued for this):

| Control | Meaning |
|---|---|
| PREDELAY | 0–250 ms |
| SIZE | Delay-line lengths (room ↔ hall) |
| DECAY | RT60, 0.2–30 s, plus FREEZE (infinite, input muted) |
| DIFFUSION | Input all-pass gain: discrete slapback ↔ smooth |
| DAMPING | High-frequency loss per pass (dark ↔ bright) |
| MODULATION | Slow line-length modulation depth (chorus in the tail; reduces metallic ringing) |

## 6. Engines

### 6.1 CLEAN

32-bit float at the host rate. No deliberate degradation.

### 6.2 ESP CHARACTER

The same algorithms, run under the constraints MAME documents (§3.2), at a
reduced internal rate. This models the *arithmetic and memory* of the period,
not Ensoniq's programs.

- Internal rate 29,761.9 Hz (the 20-voice rate, 10 MHz / 336) by default, or
  44,642.9 Hz (the review's "44 kHz" 13-voice effects, 10 MHz / 224), with
  OTIS's band-limited converter reused for host ↔ internal conversion. The third
  OTIS rate (10 MHz / 128) is offered as a non-period option.
- Signals and coefficients in 24-bit fixed point; multiplies accumulate in 48
  bits; reads of the accumulator and ALU add/subtract **saturate** to 24 bits.
- Delay memory stored as 16-bit words; writes drop the low 8 bits rounding
  toward −∞ (MAME default).
- Delay-memory budget selectable: 64K words (period-plausible; the "128 KB"
  MAME mentions for games, about 2.2 s at 29.76 kHz, shared by all lines) or
  UNLIMITED. The short budget is a deliberate creative limit; it is *not* a
  verified EPS-16 Plus figure.
- No attempt to model the 160-instruction limit as a cycle budget; it is noted
  only.

Fixed-point primitives (replacing Argon's draft):

```cpp
// 24-bit two's complement held in int32_t. Shifts on unsigned values only.
inline int32_t sat24(int64_t x) {                    // as MAME's saturate()/MAC read
    return x > 0x7FFFFF ? 0x7FFFFF : x < -0x800000 ? -0x800000 : int32_t(x);
}
inline int32_t sext24(uint32_t x) {                  // 24-bit pattern -> signed value
    x &= 0xFFFFFFu; return int32_t(x ^ 0x800000u) - 0x800000;
}
inline int16_t toDelayWord(int32_t v24) {            // MAME default: toward -infinity
    return int16_t(v24 >> 8);                        // arithmetic shift (defined in C++20; GCC/Clang/MSVC earlier too)
}
inline int32_t fromDelayWord(int16_t w) { return int32_t(w) * 256; }
inline int32_t mul24(int32_t a, int32_t b) {         // Q23 x Q23 -> Q23 via 48-bit product
    return sat24((int64_t(a) * b) >> 23);
}
```

## 7. Capture to OTIS (retroactive)

OTIS can already record what you patch into it, but only from the moment you
press REC. The FX module keeps the **recent past**:

- A rolling buffer of the post-mix output (mono sum, 16-bit, at the ESP internal
  rate in CHARACTER mode or host rate in CLEAN), LENGTH 1–20 s.
- CAPTURE (button and trigger input) freezes the last LENGTH seconds into a
  shareable slot. The audio thread only flips which half of a double buffer it
  writes; copying happens on the UI thread.
- The slot is exposed through the existing `vcv/shared/SampleShare.hpp`
  interface (version 1; its revision advances on each capture), on the module's
  right-expander pointers, exactly as OTIS shares with Vastish.
- OTIS gains a reader: menu **Import from FX on the left** (one-shot) and an
  option **Follow FX captures**. Rack layout: `[ESPish][OTIS][Vastish]`. OTIS
  reads its left neighbour and still shares to its right.
- Lookup uses the UI-owned `leftExpander.moduleId` and `Engine::getModule()`,
  as fixed in Vastish 2.2.2, not the engine-owned pointer.

So: play through the effect, hear something good after the fact, press CAPTURE,
and the reverb tail or echo trail becomes an OTIS sample to REWORK, and from
there a Vastish oscillator.

## 8. Panel and I/O (estimate ~28 HP)

| | |
|---|---|
| Inputs | IN L, IN R, CLOCK, CAPTURE trig, FREEZE gate, CV for TIME, FEEDBACK, SHIFT, DECAY, MIX (±5 V), polyphony not used |
| Outputs | OUT L, OUT R |
| Knobs | Transform: MODE (ECHO/ENSEMBLE), TIME/RATE, FEEDBACK/DEPTH, TONE, SHIFT, SPREAD/WIDTH, DRY/WET. Space: PREDELAY, SIZE, DECAY, DIFFUSION, DAMPING, MODULATION, DRY/WET. Global: ENGINE (CLEAN/ESP), MIX, CAPTURE LENGTH |
| Lights | CAPTURE ready, limiter active, FREEZE |

Layout to be checked with Rack's own component artwork at widget coordinates
before release, as done for OTIS and Vastish.

## 9. Testing (before anyone listens)

- **Fixed-point layer vs MAME:** `sat24`, accumulator saturation and the 24 → 16
  write match MAME's `saturate()`, MAC read and `round_24_to_16()` bit for bit on
  exhaustive or randomized inputs (MAME's functions copied into the test with
  attribution, BSD-3).
- **Frequency shifter:** shift a sine, measure the output frequency (± 0.5 Hz)
  and unwanted-sideband level (≥ 40 dB down, 100 Hz–10 kHz).
- **Stability:** §5.4 requirement, every engine and sample rate, 60 s at maximum
  settings, finite and bounded; then decays after FEEDBACK < 100%.
- **Reverb:** impulse response decays to −60 dB within ±15% of the DECAY setting;
  FREEZE holds energy within ±1 dB over 30 s; no denormal slowdowns on silence.
- **ESP engine:** host ↔ 29.76 kHz round trip with no aliasing above the
  converter's stopband spec; delay budget enforced.
- **Capture link:** same pattern as the Vastish link tests: a real OTIS module
  from the compiled binary importing a capture; revision handling; torn copies
  retried; no audio-thread allocation; save/restore.
- **Performance:** CLEAN and ESP each under 3% of one core at 48 kHz on the CI
  runner, measured in the test.
- CI on Linux, Windows and macOS like the other plugins.

## 10. Open questions for review

1. Is v1 scope right, or should ENSEMBLE wait so ECHO + SHIFT + SPACE ship sooner?
2. Frequency shifter range: is ±2 kHz useful, or should the top be lower with a
   finer centre?
3. ESP CHARACTER: the internal rates are inferred from the review plus OTIS
   arithmetic, and the 64K-word budget is period-plausible, not verified. An
   EPS-16 Plus service manual or schematic would settle both.
4. Capture: mono sum is simplest for OTIS (mono sample memory). Is a stereo
   capture worth exposing for future consumers?
5. Name.
