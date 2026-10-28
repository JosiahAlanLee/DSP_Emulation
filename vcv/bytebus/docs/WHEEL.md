# Wheel

**ByteBusLab 2.15 · 14 HP · a 24-stage noise register and a loop**

Contract for ByteBusLab 2.15. Two 24-stage shift registers on one clock. FREE
is a maximal-length feedback shift register with the all-zero state added, so
it visits every 24-bit state once in 2^24 = 16,777,216 steps. LOOP recirculates
its first LENGTH stages, holding a pattern that repeats every LENGTH steps,
until CAPTURE copies FREE into it. Both are read eight stages at a time
through one window and published as two
[byte buses](../../../docs/BYTE_BUS_SPEC.md) with the
[Clocked Register](MANUAL.md)'s clock rules; LOOP also drives a DAC, a
bell-curve output and a gate. The design reference is the Nonlinearcircuits
Pseudo by Andrew Fitch, after Electronotes 76; how Wheel relates to it, and
what its documentation establishes, is in
[`docs/NLC_BUS_SPEC.md` §4](../../../docs/NLC_BUS_SPEC.md#4-wheel-from-pseudo).
The panel, names, taps and the bus side are original.

## What it is for

- **A looping random source.** LOOP repeats a random pattern of LENGTH steps
  on U (stepped CV), G (a smoother, bell-curved CV) and LOOP GATE; CAPTURE
  replaces it with fresh material on the beat.
- **A timbre from noise.** With CLOCK from an oscillator, LENGTH *L* makes a
  periodic wave at clock / *L* whose shape is the loop's contents. A capture
  changes the timbre without changing the pitch.
- **Noise.** FREE BUS and NOISE at a fast clock, at any LENGTH, are a long
  pseudo-random stream that never repeats within a quarter of an hour at
  18 kHz. LENGTH Off joins the two registers into one 48-stage line, so LOOP
  BUS, U, G and LOOP GATE carry the same stream 24 steps later.
- **Bytes for other modules.** LOOP BUS and FREE BUS feed Lookup, Switch,
  Fan's address or Parallel Sum's mask.

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Selector: Auto (CLOCK jack, else Internal), CLOCK jack, Internal, Engine rate. There is no bus input, so no Bus CLK choice. |
| STRIDE | Selector: 1 (default), 2 or 3. See [Reading](#reading). |
| RATE | Internal clock, 0.125–2048 Hz, default 8 Hz. Used only when Internal owns the clock. |
| LENGTH | Off, or 1–24 (default 16): the loop's length in steps. |
| OFFSET | Stage 1–24, default 1: the window's first stage. |
| SEED | Byte 00–FF, default 00. RESET and LOAD set both registers to it, repeated in all three bytes. |
| LOAD | Loads SEED, restarts the internal clock phase and publishes. |
| CAPTURE (button) | Arms a capture; its light shows it armed. |
| CLOCK | Monophonic clock; rising at 1 V, falling at 0.1 V. |
| RESET | Monophonic trigger, same thresholds; loads SEED on its rising edge. |
| CAPTURE (input) | Trigger, same thresholds; arms a capture. |
| LOOP BUS, FREE BUS | The window on each register: 9 channels with a strobe on each step, or 8 at Engine rate. |
| CLK OUT | The CLK channel of both buses as a gate. |
| U | The LOOP word as a DAC: (*n* − 128) × 5 / 128 V, −5 to +4.96 V. |
| G | The LOOP word's set bits counted with equal weight: (count − 4) × 1.25 V, nine levels from −5 to +5 V. |
| LOOP GATE | LOOP's stage 1, 0/10 V. |
| NOISE | FREE's stage 1, the newest feedback bit, 0/10 V. |

The readout shows the LOOP word in hexadecimal; eight lights show its bits,
bit 7 at the left. Stages are numbered 1–24 on the panel and 0–23 below.

## The registers

On each acquisition edge, stage 0 holding the newest bit:

- **FREE** shifts in the XOR of its stages 23, 22, 21 and 16 (the polynomial
  x^24 + x^23 + x^22 + x^17 + 1, maximal: 2^24 − 1 steps), inverted while
  stages 0–22 are all zero. The inversion adds the all-zero state, between
  800000 and 000001, so the cycle is every 24-bit state once and no seed can
  lock it.
- **LOOP** shifts in its own stage LENGTH − 1: its first LENGTH stages form a
  ring and the stages past it carry delayed copies. With LENGTH Off it shifts
  in the bit leaving FREE (FREE's stage 23 before the shift): the two registers
  are one 48-stage line.
- **CAPTURE** arms on a rising edge of the jack or a press of the button. On
  the next acquisition edge FREE shifts and then LOOP becomes a copy of FREE,
  all 24 stages, instead of shifting. A capture armed when RESET or LOAD
  arrives waits for the following acquisition edge.
- **RESET** and **LOAD** set both registers to SEED × 010101 hex and publish
  them, per ClockOwner. Seed 00 is valid: FREE leaves the all-zero state on
  its next step.

LENGTH changes act on the next edge. A loop shorter than the window shows the
ring's repetition across the window's later bits.

## Reading

Both buses read eight stages through one window: bit *k* of the word is stage
(OFFSET − 1 + STRIDE × *k*) mod 24, *k* = 0 … 7.

- STRIDE 1 reads eight consecutive stages: U changes as a shifting binary
  number, and G moves by at most one level per step, the Pseudo's "glitchy
  sine".
- STRIDE 3 spreads the window across all 24 stages: successive words are less
  related.
- G counts with equal weight. Over FREE's full cycle every byte appears
  equally often in any window, so G's nine levels occur exactly in the
  proportions 1, 8, 28, 56, 70, 56, 28, 8, 1 in 256: a bell curve.

## Clock, reset and strobe timing

Clock ownership is the shared `src/engine/ClockOwner.hpp`, as on Counter: Auto
selection by connection, an initially high clock counting as its first edge,
one-sample strobes with a mandatory low sample, RESET priority over a
simultaneous edge, the seed published after RESET or LOAD with acquisition
suppressed for that strobe and the low sample after it. Both buses share one
strobe. Engine rate steps every sample and emits no clock channel. Each cable
hop costs one engine sample.

## Saving and reset

Rack saves the controls and both registers, as `"free"` and `"loop"`, each
bounded to 24 bits. Reload publishes them; it does not resume the internal
clock phase or an armed capture. Rack's Initialize restores the defaults
(LENGTH 16, STRIDE 1, OFFSET 1, SEED 00) and clears both registers.

## First patch

1. **Wheel** with nothing patched: Internal at 8 Hz, LENGTH 16. Both registers
   start at zero; press CAPTURE to fill the loop. U → an oscillator's V/OCT:
   a 16-step random melody that repeats.
2. Press CAPTURE again: a new melody, the same length and timing.
3. A clock divider's output (or a **Counter**'s CARRY) → CAPTURE: the melody
   changes every few bars, on the beat.
4. G instead of U: a smoother line that stays nearer the middle.
5. A VCO square at 2 kHz → CLOCK, U → a lowpass → your mixer: a tone at
   2 kHz / LENGTH. Capture to change its timbre; change LENGTH to change its
   pitch.
6. NOISE → a VCA: long-period digital noise. With LENGTH Off, U and G are
   noise too.

## Validation

Portable tests (`tests/engine_test.cpp`): FREE's full 2^24 cycle visiting
every state once, and the same taps without the inversion maximal at
2^24 − 1; G's counts over the cycle exactly binomial; every window against
stage-by-stage reads for every OFFSET and STRIDE; U and G levels; every
LENGTH's loop period, exact from a single bit, and the whole register repeating
after 24 steps; Off as one 48-stage line; capture on the next edge, from the
input and the button, and under RESET; reset replay under random clocks with
strobe gaps; the internal clock at 44.1/48/96 kHz.

Rack API tests (`tests/module_test.cpp`): real ports and lights against an
engine fed the same controls, at Engine rate through a capture, LENGTH Off and
another window, with U, G and both gates; capture from the jack on the CLOCK
jack; persistence of both registers with bounded restore, Initialize; a
Counter's clock and CARRY driving Wheel's CLOCK and CAPTURE, in random module
order, identical at 44.1/48/96 kHz.

Software tests do not establish physical clock transport.
