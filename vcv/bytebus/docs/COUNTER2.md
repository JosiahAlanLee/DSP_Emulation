# Counter 2

**ByteBusLab · 12 HP · a phase word at N/D of its clock**

Contract for ByteBusLab 2.11. A latching module under the
[byte bus](../../../docs/BYTE_BUS_SPEC.md) rules (§5), with the
[Clocked Register](MANUAL.md)'s clock ownership and strobe timing. Each
acquisition edge advances a phase word by N/D of a cycle. Where
[Counter](COUNTER.md) adds a byte, Counter 2 adds a ratio, kept exactly with an
integer accumulator, so it is a second module rather than a mode of the first.
The role reference is the Via's SYNC, a contour at a ratio of an input clock;
this is an original per-edge implementation with no interpolation between edges.

## What it is for

- **Polymeter locked to a clock.** On Gorgon's CLOCK (one pulse per row), N 1,
  D 12 is a cycle every three beats against the four-beat bar, and D 48 one every
  three bars of sixteen rows. CARRY marks each cycle; the bar and the cycle meet
  again after the least common multiple of their lengths.
- **A full-range phase for any cycle length.** D 12 gives 12 evenly spaced
  words over 00–FF, so Lookup reads its whole table once per cycle (Counter's
  MOD 12 gives the words 0–11 instead).
- **Ratios.** N 3, D 16: three cycles in sixteen edges, a 3-against-16 cross
  rhythm through Lookup Rhythm or Density.
- **Audio.** At Engine rate the word is a sawtooth at `N/D` of the sample rate:
  N 1, D 64 is 750 Hz at 48 kHz.
- **Ratios from a grid.** A knob or CV through Converter's ADC into
  [Lookup](LOOKUP.md)'s Ratio family, and Lookup's BUS OUT into RATIO: the
  control sweeps through a grid of musical ratios (straight, triplet, dotted,
  integers…) instead of every n/d.

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Selector: Auto (CLOCK jack, else internal), CLOCK jack, Internal, Engine rate. There is no Bus CLK choice: RATIO's clock latches the ratio word, never the phase. |
| RATE | Internal clock, 0.125–2048 Hz. Used only when Internal owns the clock. |
| N | Integer 1–16, default 1. Cycles per D edges. |
| D | Integer 1–64, default 16. Edges per N cycles. |
| RATIO | Byte bus (appended in 2.11). Patched, its word is a *ratio word* and replaces N and D: n = high four bits + 1, d = low four bits + 1, so 1/16 to 16. A clocked RATIO uses its latched word. The N/D readout shows the ratio in force. |
| SEED | Integer 0–255. The phase word RESET/LOAD starts from. |
| LOAD | Loads SEED, restarts the internal clock phase and publishes the seed. |
| CLOCK | Monophonic clock; rising at 1 V, falling at 0.1 V. |
| RESET | Monophonic trigger, same thresholds; loads SEED on its rising edge. |
| BUS OUT | 9 channels, exact 0/10 V bits, channel 9 the word clock. Engine mode emits 8 channels. |
| CLK OUT | The same one-engine-sample publication pulses as BUS OUT's CLK. Always low in Engine mode. |
| CARRY | One-engine-sample pulse on an edge that completes a cycle. |

Two readouts show the published word in hex and the ratio N/D.

## Counting

The phase is held as an integer `acc` in units of `1/(256 · D)` of a cycle;
the published word is `acc / D`, rounded down. On an acquisition edge:

```
acc   = acc + 256 · N
carry = acc >= 256 · D
acc   = acc mod (256 · D)
```

So from SEED `s`, after *e* edges the word is
`floor(256 · frac(s/256 + e · N / D))`, exactly, for ever: no rounding
accumulates. CARRY is high on the edges where the cycle count rises; with N
greater than D a cycle can complete more than once in an edge, and CARRY still
pulses once.

- Ratios above 1/2 step more than half a cycle per edge, so the word appears to
  run backwards or jump; that is the per-edge sampling, and it is often a useful
  rhythm through Lookup. There is no interpolation between edges.
- Changing N takes effect on the next edge. Changing D keeps the current word:
  the accumulator restarts from `word · D`, dropping the fraction below it.
- In Engine mode every sample is an edge.

## Clock, reset and strobe timing

Identical to Clocked Register's [clock ownership](MANUAL.md#clock-ownership)
and [clock and reset timing](MANUAL.md#clock-and-reset-timing), through the
shared `src/engine/ClockOwner.hpp`, as Counter's: seed publication after
LOAD/RESET with acquisition suppressed for that strobe and the low sample after
it, RESET priority over a simultaneous edge, one-sample strobes with a
mandatory low sample.

## Saving and reset

Rack saves the controls and the exact phase (`"accumulator"` and
`"denominator"`), so a reloaded Counter 2 continues the same sequence. Values are
bounded on load. Reload publishes the saved word; it does not resume the
fractional internal clock phase. Initialize restores N 1, D 16, word 00.
A ratio from RATIO is not saved: it is whatever the cable carries.

## First patch

1. **Counter 2** with CLOCK from Gorgon's CLOCK, N 1, D 12; a **Counter** on
   the same clock with MOD 16. Both CARRYs into two triggers: three-beat cycles
   against four-beat bars, meeting every 48 rows.
2. Counter 2 BUS OUT → **Lookup** (Density) → Split → triggers: the whole
   pattern plays once every three beats.
3. N 3, D 16: three Density cycles per bar, a cross rhythm.
4. A knob into **Converter**'s ADC, its BUS OUT into a **Lookup** set to Ratio
   (SHAPE 39 is the Straight grid in full), and Lookup's BUS OUT into RATIO: the
   knob steps through 1/16 … 16 in powers of two.

## Validation

Portable tests (`tests/engine_test.cpp`): every N 1–16 and D 1–64 from seeds 0
and 77 against the exact formula, with CARRY on exactly the edges whose cycle
count rises; 1/12 against 1/16 carrying 4 and 3 times in 48 edges and meeting at
00; a D change keeping the word; RESET loading SEED and winning over an edge;
strobe gaps under random clocks, resets and D changes; the internal clock at
44.1/48/96 kHz.

Rack API tests (`tests/module_test.cpp`): real ports; the 12/16 polymeter on
one clock, with every word of the 12-edge cycle checked; a save mid-sequence
restored into a fresh module that continues identically; bounded restores;
Initialize; a ratio word on RATIO replacing the knobs and the knobs returning
when it is unpatched; Lookup Ratio into RATIO through one-sample cables in random
order, giving each chosen ratio's carries; at 44.1/48/96 kHz.

Software tests do not establish physical clock transport.
