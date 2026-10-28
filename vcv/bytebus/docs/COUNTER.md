# Counter

**ByteBusLab · 12 HP · an 8-bit word that counts**

Contract for ByteBusLab 2.4. Implements the
[byte bus](../../../docs/BYTE_BUS_SPEC.md) latching-module rules (§5) with the
[Clocked Register](MANUAL.md)'s clock ownership and strobe timing, and adds
one operation: on each acquisition edge the held word moves by the increment,
wrapping at the modulus (Loop, `(word + increment) mod modulus`), holding at
its top (One-shot) or reflecting at both ends (Pendulum). The hardware role
reference is a binary phase accumulator (Erfurt). Nothing here reuses
manufacturer names, tables or behaviour beyond that public role; the modulus,
carry output, bus-sourced increment and run modes are original.

## What it is for

- Bits as clock dividers: with increment 1 and modulus 256, bit *n* toggles
  every 2^*n* edges, so BUS OUT through Core **Split** is a divider chain
  /2 … /256, and a Machinemodule in bus TRIG mode plays the binary-counter
  rhythm.
- Bytebeat: Counter is `t`. Lookup, Vastish FUN and a second Counter are
  `t>>n`, `t&m`, `t^k` and `f[t]`.
- A phase word for Lookup (address) or a Converter DAC (ramp), at any rate.
- A step counter: modulus 16 and increment 1 is a sixteen-step index; CARRY
  is the bar.
- An envelope: One-shot, with a gate on RESET. Through a Converter DAC the
  word rises at RATE × INC words per second and holds at `MOD − 1`; CARRY
  marks the arrival. Lookup's Curve family bends it, and Curve shape 32 is
  `255 − n`, so at MOD 256 the same run falls from FF and ends held at 00.
- A triangle: Pendulum through a Converter DAC. At Engine rate with MOD 256 and
  INC 1 a cycle is 510 samples, 94 Hz at 48 kHz; INC raises it.

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Selector: Auto, CLOCK jack, Bus CLK, Internal, Engine rate. Same meaning and selection rules as Clocked Register. |
| RATE | Internal clock, 0.125–2048 Hz. Used only when Internal owns the clock. |
| INC | Integer 1–255. The increment when BUS IN is unpatched. In Loop, 255 counts down by one at modulus 256; in general `modulus − k` counts down by *k*. In One-shot and Pendulum it is always a step up or a magnitude. |
| MOD | Integer 2–256, default 256. The modulus: Loop wraps at it, and One-shot and Pendulum stop at `MOD − 1`. |
| MODE | Selector: Loop (default), One-shot, Pendulum. See [Run modes](#run-modes). |
| SEED | Integer 0–255. The word loaded by RESET/LOAD. Changing the knob does not alter the held word. |
| LOAD | Loads SEED, restarts the internal clock phase and publishes the seed. |
| BUS IN | Optional increment source: 8 data channels, or 9 with CLK. The word on the data lines is the increment, 0–255 (0 holds; in Loop, 255 is −1 at modulus 256). Fewer data channels mean a smaller increment, never mono broadcast. When patched it replaces INC. |
| CLOCK | Monophonic clock; rising at 1 V, falling at 0.1 V. |
| RESET | Monophonic trigger, same thresholds; loads SEED on its rising edge. |
| BUS OUT | 9 channels, exact 0/10 V bits, channel 9 the word clock. Engine mode emits 8 channels. |
| CLK OUT | The same one-engine-sample publication pulses as BUS OUT's CLK. Always low in Engine mode. |
| CARRY | One-engine-sample pulse on an acquisition edge that wrapped past the modulus (Loop), arrived at the hold (One-shot) or reflected (Pendulum). Low otherwise. |

Hex display 00–FF and eight LEDs, bit 7 at the left, as Register. Lights beside
LOAD, CLK OUT and CARRY show seed publication, word clocks and carries.

## Counting

This section is Loop, the default MODE, and all of ByteBusLab 2.3's Counter.
On an acquisition edge, with `inc` the increment in force on that sample:

```
next  = word + inc
carry = next >= MOD
word  = next mod MOD
```

- `inc` is BUS IN's current decoded data lines (`rawWord`) when BUS IN is
  connected, reclocked by this module's selected edge as Register reclocks its
  data; otherwise INC. A connected BUS IN carrying 00 holds the count.
- A held word at or above MOD (SEED above MOD, or MOD lowered below the held
  word) is published as it is and takes the modulus on the next edge:
  `(word + inc) mod MOD`, with CARRY. Nothing is clamped silently.
- Changing MOD or INC between edges changes nothing until the next edge.
- CARRY is a one-sample pulse on the edge sample, coincident with the word
  clock strobe. It is a Rack signal, not a promise about hardware capture;
  for ordinary trigger inputs it is at least one engine sample high.
- Counting down (`MOD − k`), the sum passes MOD on every edge except the one
  that goes below zero, so CARRY is high on all those edges and low on that
  one: a borrow is the absence of a carry.
- In Engine mode every sample is an edge, so CARRY can be high on consecutive
  samples (MOD 2 carries every other sample).
- A chained second Counter clocked from CARRY is a 16-bit counter whose high
  byte advances one hop later than the low byte; the hop is the documented
  one-sample cable delay.

## Run modes

MODE chooses what an edge does at the ends of the range `0 … MOD − 1`. In every
mode `inc` comes from INC or BUS IN as above, a connected BUS IN carrying 00
holds the word with CARRY low, and Engine rate applies the same rule on every
sample.

| MODE | An edge | CARRY |
|---|---|---|
| Loop | `(word + inc) mod MOD` ([Counting](#counting)) | on a wrap |
| One-shot | `min(word + inc, MOD − 1)`: counts up only and holds at the top | on the edge that arrives at `MOD − 1` |
| Pendulum | moves `inc` in its direction; a step that reaches or passes an end stops on that end and reverses | on each reflection |

One-shot:

- The increment is always added, so `MOD − k` is a large step up, not "down by
  *k*". A falling envelope is the rising one through Lookup's Curve shape 32,
  ending at 00 when MOD is 256.
- At `MOD − 1` further edges publish it again with CARRY low. RESET or LOAD
  loads SEED and starts the next run, which is how a gate retriggers an
  envelope. A SEED of `MOD − 1` starts held, with no CARRY.

Pendulum:

- The increment, from INC or BUS IN, is a magnitude. The direction is the
  module's own state: up after RESET/LOAD, saved with the patch, and changed
  only by a reflection or RESET/LOAD.
- An overshoot clamps to the end word rather than folding back, so every swing
  reaches 0 and `MOD − 1` and shows each for one edge. MOD 8, INC 3:
  `0 3 6 7 4 1 0 3 …`, with CARRY on the 7 and the 0.
- Counting up from `MOD − 1` (a SEED there, or MOD lowered onto the word) is a
  step that reaches the top: the word stays, the direction turns down, and
  CARRY is high. Down from 0 likewise.
- An increment of `MOD − 1` or more alternates between the ends, with CARRY
  on every edge; MOD 2 at Engine rate carries on every sample.

Both:

- A held word at or above MOD (SEED above MOD, or MOD lowered below the word)
  is published as it is and goes to `MOD − 1` on the next edge, with CARRY,
  even with a 00 increment; Pendulum turns down there. Loop wraps it instead.
- MODE changes take effect on the next edge, from the current word. Loop and
  One-shot neither use nor change the Pendulum direction, so returning to
  Pendulum resumes the direction it had.

## Clock, reset and strobe timing

Identical to Clocked Register's [clock ownership](MANUAL.md#clock-ownership) and
[clock and reset timing](MANUAL.md#clock-and-reset-timing): Auto selection by
connection, external edge detectors tracked while unselected, publication of the
held state before acquisition after a fresh start, Initialize or restore,
one-sample strobes with a mandatory low sample, RESET priority over a
simultaneous edge, seed publication after LOAD/RESET with acquisition suppressed
for that strobe and the following low sample, held-high RESET not reloading.
Engine rate counts every sample and emits no clock channel.

Both modules run the same state machine, `src/engine/ClockOwner.hpp`; Counter
adds only its word and what an edge does to it.

## Saving and reset

Rack saves controls, the held word and the Pendulum direction. Reload
publishes the saved word; it does not resume the fractional internal clock
phase. Rack's Initialize restores default controls (Loop), word 0 and direction
up. A ByteBusLab 2.3 patch has neither MODE nor a direction, and loads as Loop,
counting up. No DAC output: send BUS OUT to Converter, or to
Lookup's BUS IN whose AUDIO output is a raw DAC.

## First patch

[`examples/counter-divider.vcv`](../examples/counter-divider.vcv) is steps 1–3.

1. **Counter** with SOURCE Auto, nothing patched: Internal at 8 Hz, INC 1,
   MOD 256. The display climbs 00…FF and wraps; CARRY pulses once per 256 edges.
2. BUS OUT → Core **Split**. Outputs 1–8 are square waves at /2, /4 … /256 of
   RATE.
3. BUS OUT → **Lookup** BUS IN, FAMILY Rhythm; Lookup BUS OUT → Split → eight
   trigger inputs. The address visits every rhythm step in order; SEED and
   SHAPE change the pattern.
4. Set MOD 16: a sixteen-step index. CARRY → a second Counter's CLOCK, which
   then counts bars; or CARRY → Register CLOCK to capture a word once per bar.
5. Patch a Register's BUS OUT into Counter's BUS IN: the increment is now a
   word of state, and the two feed back through Vastish FUN logic as the
   Register manual's feedback patch does.
6. MODE One-shot, MOD 256, a gate into RESET, BUS OUT → Converter BUS IN, DAC
   OUT → a VCA's CV: each gate restarts a ramp that climbs at RATE × INC words
   per second and holds. Lookup (Curve, SHAPE 32) between Counter and Converter
   turns it into a decay that ends at 00.
7. MODE Pendulum: the DAC is a triangle, and CARRY pulses at each peak and
   trough.

## Validation

Portable tests (`tests/engine_test.cpp`): every (word, inc, MOD) for all
256 × 256 × 255 combinations through `tick`, with carry on exactly the
wrapping edges; INC vs BUS IN precedence, a BUS IN of 00 holding, and
reclocking of raw data lines; counting down; held word and SEED above MOD;
MOD changed between edges; clock ownership and frozen clocks; reset priority;
seed publication and suppressed acquisition; strobe gaps under random clocks
and resets; Engine-mode carries; internal timing at 44.1/48/96 kHz. Run modes:
every (word, inc, MOD) in One-shot and every (word, inc, MOD, direction) in
Pendulum against the table above; the MOD 8, INC 3 sequences; every swing
reaching both ends with CARRY exactly there; 00 holding at the ends; RESET
setting the direction up; MODE changes keeping it; over-modulus words; Engine
rate; One-shot envelopes retriggered by RESET at 44.1/48/96 kHz; strobe gaps
and range under random clocks, resets, increments and MODE changes. Register's
tests pass unchanged against the shared state machine.

Rack API tests (`tests/module_test.cpp`): real ports and lights, JSON restore
and saved-word publication, LOAD, Engine rate, Initialize, and bounded saved
words; Pendulum through the panel with its direction saved mid-descent and
resumed, a 2.3 patch loading as Loop and up, Initialize restoring both, and
One-shot holding and retriggering from RESET; One-shot and Pendulum through
Lookup Curve 32 as `255 − count` at 44.1/48/96 kHz; CARRY → Counter CLOCK and CARRY → Register CLOCK with Register BUS IN
from Counter BUS OUT, through delayed cables in random module order; Counter →
Lookup Rhythm → Split producing the gate sequence the portable Lookup engine
computes directly, with a low half-step between steps, at 44.1/48/96 kHz.

Software tests do not establish physical clock transport.

## First sample after Initialize or restore

A fresh instance and Initialize start at 00; restore starts from the saved word.
The first sample publishes that word without acquiring from an already-high
clock. A RESET or LOAD already high on that sample takes priority and loads
SEED instead; holding it high does not reload or block later clock edges. BUS IN
replaces INC from its raw lines on the next acquisition, including when its
own CLK is stopped.
