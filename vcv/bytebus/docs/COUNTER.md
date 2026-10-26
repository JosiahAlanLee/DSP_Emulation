# Counter

**ByteBusLab · 12 HP · an 8-bit word that counts**

Contract for ByteBusLab 2.3. Implements the
[byte bus](../../../docs/BYTE_BUS_SPEC.md) latching-module rules (§5) with the
[Clocked Register](MANUAL.md)'s clock ownership and strobe timing, and adds
one operation: on each acquisition edge the held word becomes
`(word + increment) mod modulus`. The hardware role reference is a binary
phase accumulator (Erfurt). Nothing here reuses manufacturer names, tables or
behaviour beyond that public role; the modulus, carry output and bus-sourced
increment are original.

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

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Selector: Auto, CLOCK jack, Bus CLK, Internal, Engine rate. Same meaning and selection rules as Clocked Register. |
| RATE | Internal clock, 0.125–2048 Hz. Used only when Internal owns the clock. |
| INC | Integer 1–255. The increment when BUS IN is unpatched. 255 counts down by one at modulus 256; in general `modulus − k` counts down by *k*. |
| MOD | Integer 2–256, default 256. The modulus. |
| SEED | Integer 0–255. The word loaded by RESET/LOAD. Changing the knob does not alter the held word. |
| LOAD | Loads SEED, restarts the internal clock phase and publishes the seed. |
| BUS IN | Optional increment source: 8 data channels, or 9 with CLK. The word on the data lines is the increment, 0–255 (0 holds; 255 is −1 at modulus 256). Fewer data channels mean a smaller increment, never mono broadcast. When patched it replaces INC. |
| CLOCK | Monophonic clock; rising at 1 V, falling at 0.1 V. |
| RESET | Monophonic trigger, same thresholds; loads SEED on its rising edge. |
| BUS OUT | 9 channels, exact 0/10 V bits, channel 9 the word clock. Engine mode emits 8 channels. |
| CLK OUT | The same one-engine-sample publication pulses as BUS OUT's CLK. Always low in Engine mode. |
| CARRY | One-engine-sample pulse on an acquisition edge whose addition wrapped past the modulus. Low otherwise. |

Hex display 00–FF and eight LEDs, bit 7 at the left, as Register. Lights beside
LOAD, CLK OUT and CARRY show seed publication, word clocks and carries.

## Counting

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

## Clock, reset and strobe timing

Identical to Clocked Register's [clock ownership](MANUAL.md#clock-ownership)
and [clock and reset timing](MANUAL.md#clock-and-reset-timing): Auto
selection by connection, external edge detectors tracked while unselected, an
initially high clock counting as its first edge, one-sample strobes with a
mandatory low sample, RESET priority over a simultaneous edge, seed
publication after LOAD/RESET with acquisition suppressed for that strobe and
the following low sample, held-high RESET not reloading. Engine rate counts
every sample and emits no clock channel.

Both modules run the same state machine, `src/engine/ClockOwner.hpp`; Counter
adds only its word and what an edge does to it.

## Saving and reset

Rack saves controls and the held word. Reload publishes the saved word; it
does not resume the fractional internal clock phase. Rack's Initialize restores
default controls and word 0. No DAC output: send BUS OUT to Converter, or to
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

## Validation

Portable tests (`tests/engine_test.cpp`): every (word, inc, MOD) for all
256 × 256 × 255 combinations through `tick`, with carry on exactly the
wrapping edges; INC vs BUS IN precedence, a BUS IN of 00 holding, and
reclocking of raw data lines; counting down; held word and SEED above MOD;
MOD changed between edges; clock ownership and frozen clocks; reset priority;
seed publication and suppressed acquisition; strobe gaps under random clocks
and resets; Engine-mode carries; internal timing at 44.1/48/96 kHz. Register's
tests pass unchanged against the shared state machine.

Rack API tests (`tests/module_test.cpp`): real ports and lights, JSON restore
and saved-word publication, LOAD, Engine rate, Initialize, and bounded saved
words; CARRY → Counter CLOCK and CARRY → Register CLOCK with Register BUS IN
from Counter BUS OUT, through delayed cables in random module order; Counter →
Lookup Rhythm → Split producing the gate sequence the portable Lookup engine
computes directly, with a low half-step between steps, at 44.1/48/96 kHz.

Software tests do not establish physical clock transport.
