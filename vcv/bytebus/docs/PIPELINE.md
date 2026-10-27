# Pipeline

**ByteBusLab · 12 HP · a word returns DELAY edges later**

Contract for ByteBusLab 2.8. Implements the
[byte bus](../../../docs/BYTE_BUS_SPEC.md) latching-module rules (§5) with the
[Clocked Register](MANUAL.md)'s clock ownership and strobe timing. On each
acquisition edge the input word enters a history, and Pipeline publishes the
word that entered DELAY edges earlier. LOOP repeats the last DELAY words;
SPREAD delays each bit a little more than the one below it. The hardware role
reference is a pipeline delay with loop and scramble (Rostock). Nothing here
reuses manufacturer names, tables or behaviour beyond that public role; SPREAD
and the saved history are original.

## What it is for

- **The canon of the kit.** A rhythm word (Counter → Lookup Rhythm or Density)
  into Pipeline is the same pattern DELAY steps later. Combine the two with
  Switch (Bits, or Select on a bar bit) and the kit answers itself; see
  [First patch](#first-patch) for which input takes which.
- **Lanes entering in turn.** With SPREAD, lane *k* of the word arrives
  *k* × SPREAD steps after lane 0: SPREAD 2 on an eight-lane pattern is a
  round in which each drum joins two steps after the one below it.
- **Freezing a phrase.** LOOP repeats whatever was in the last DELAY steps,
  from a gate (Gorgon's FILL bit through Split, a Counter's top bit) or the
  button. Release it and the live pattern returns DELAY steps later.
- **A shift register.** At DELAY 1 the output is the previous edge's word;
  chained Pipelines and Registers are a byte shift register with any length up
  to 64 per module.

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Selector: Auto, CLOCK jack, Bus CLK, Internal, Engine rate. Same meaning and selection rules as Clocked Register. |
| RATE | Internal clock, 0.125–2048 Hz. Used only when Internal owns the clock. |
| DELAY | Integer 1–64, default 16. How many edges after it entered a word is published. |
| SPREAD | Integer 0–8, default 0. Bit *k* is delayed `k × SPREAD` edges more than bit 0. |
| LOOP | Latching button with a light. While on, or while LOOP is high, the history recirculates. The light shows the loop in force from either. |
| CLEAR | Fills the history with 00, restarts the internal clock phase and publishes 00, as RESET does. |
| BUS IN | 8 data channels, or 9 with CLK. Its raw data lines enter on this module's edge, as Register reclocks its data. Unpatched, 00 enters. |
| CLOCK | Monophonic clock; rising at 1 V, falling at 0.1 V. |
| RESET | Monophonic trigger, same thresholds; clears the history on its rising edge. |
| LOOP (input) | Gate, same thresholds: loops while high. |
| BUS OUT | 9 channels, exact 0/10 V bits, channel 9 the word clock. Engine mode emits 8 channels. |
| CLK OUT | The same one-engine-sample publication pulses as BUS OUT's CLK. Always low in Engine mode. |

Hex display and eight LEDs, bit 7 at the left, show the published word.

## Delay

Pipeline keeps the last 128 words that entered, one per acquisition edge. With
`w(n)` the word that entered on edge *n*, the word published on edge *n* is

```
bit k of out(n) = bit k of w(n − DELAY − k · SPREAD)
```

with words before the history began, or before the last clear, read as 00. At
most `64 + 7 × 8 = 120` edges back, so 128 stages always suffice.

- The word holds between edges. DELAY and SPREAD are read when a word is
  published, so turning them between edges changes nothing until the next one.
- Changing DELAY changes which stored word is read; the history itself is
  untouched, so shortening and lengthening the delay never loses words.
- In Engine mode every sample is an edge.

## Loop

While LOOP is on, the word that enters on each edge is the one leaving the
delay, `w(n) = w(n − DELAY)`, instead of BUS IN. The words already in the delay
come out when they are due, so the first DELAY edges of a loop are the ones that
would have played anyway; after that the same DELAY words repeat, with period
DELAY. BUS IN is ignored.

When LOOP opens, BUS IN enters again, and it reaches the output DELAY edges
later: the loop plays out once more, then the live input follows. SPREAD reads
the recirculating words like any others, so the lanes of a looped pattern stay
offset. Changing DELAY during a loop changes the loop's length from the next
edge, starting from the words it then finds.

## Clock, reset and strobe timing

Identical to Clocked Register's [clock ownership](MANUAL.md#clock-ownership)
and [clock and reset timing](MANUAL.md#clock-and-reset-timing), through the
shared `src/engine/ClockOwner.hpp`: Auto selection by connection, an initially
high clock counting as its first edge, one-sample strobes with a mandatory low
sample, RESET priority over a simultaneous edge, the cleared word (00) published
after RESET or CLEAR with acquisition suppressed for that strobe and the low
sample after it.

## Saving and reset

Rack saves the controls and the whole history, as `"history"`: 128 words,
oldest first. Reload publishes the word the restored history and controls give;
it does not resume the fractional internal clock phase. A shorter list fills the
latest stages, and values are bounded to a byte. Rack's Initialize restores the
default controls and an empty history.

## First patch

1. **Counter** (CLOCK from a clock, INC 8) → **Lookup** (Rhythm) → **Pipeline**
   BUS IN; Pipeline in Auto follows the Lookup's clock. DELAY 4: the pattern
   comes out a beat later.
2. Pipeline BUS OUT → **Switch** A, Lookup BUS OUT → Switch B, MODE Bits,
   C WORD 0F: lanes 0–3 play the pattern and lanes 4–7 its echo, exactly
   DELAY steps later. Pipeline goes into A because its word arrives one cable
   hop after the Lookup's, and Switch reads B on A's edges; the other way
   round, the echo would be one step later than DELAY.
3. SPREAD 1: the echo's lanes arrive one step apart.
4. Patch Counter's top bit through Split into LOOP: every other bar repeats the
   last DELAY steps.

## Validation

Portable tests (`tests/engine_test.cpp`): every DELAY 1–64 with SPREAD 0, 1, 3
and 8 against a reference history of random words; LOOP's first DELAY words,
its period and its release at DELAY 1, 3, 16 and 64; the word held between
edges; a DELAY change acting on the next edge; RESET clearing and winning over a
simultaneous edge; strobe gaps under random clocks and resets; the internal
clock at 44.1/48/96 kHz.

Rack API tests (`tests/module_test.cpp`): real ports and lights; Counter →
Lookup Rhythm → Pipeline with SPREAD through one-sample cables in random module
order, identical at 44.1/48/96 kHz; LOOP from the gate and the button; a looped
phrase saved and restored into a fresh module that goes on identically; short
and out-of-range saved histories; CLEAR and Initialize.

Software tests do not establish physical clock transport.
