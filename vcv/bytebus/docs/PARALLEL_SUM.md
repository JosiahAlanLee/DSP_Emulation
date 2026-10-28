# Parallel Sum

**ByteBusLab 2.14 · 20 HP · a shift register summed through eight weights**

Contract for ByteBusLab 2.14. An eight-stage shift register whose new bit is
the parity of the stages MASK taps, inverted by XNOR, XOR a comparator on
INJECT. Each stage has a weight; the weighted stages sum to a stepped CV and a
zero-mean AUDIO output, so the weights are the coefficients of a filter on the
register's bit stream at the clock rate. The register is published on a
[byte bus](../../../docs/BYTE_BUS_SPEC.md) under the
[Clocked Register](MANUAL.md)'s clock rules, and a byte bus can set the mask.
The design reference is the Nonlinearcircuits Burhans Timbre by Andrew Fitch,
after Bill Burhans' timbre generator; how Parallel Sum relates to it, and what
its build document establishes, is in
[`docs/NLC_BUS_SPEC.md` §5](../../../docs/NLC_BUS_SPEC.md#5-parallel-sum-from-burhans-timbre).
The panel, names and the bus side are original.

## What it is for

- **A timbre generator.** CLOCK from an oscillator: the register cycles at the
  clock rate, and a cycle of *n* steps is a tone at clock / *n*. The weights
  shape it: all +1 is a moving sum (a lowpass), alternating signs a highpass,
  one weight a single delayed bit. AUDIO is zero-mean.
- **A CV pattern sequencer.** At a slow clock, CV OUT is a stepped pattern
  whose levels are the weights of the set stages; MASK chooses how long and
  which pattern.
- **Controllable noise.** A maximal mask at a fast clock is a 255-step
  pseudo-random stream; fewer taps, shorter loops.
- **Patterns from outside.** INJECT XORs any gate or audio, through a
  comparator, into each new bit; GATE OUT patched into INJECT taps stage 1 as
  well, changing the loop. A byte bus on MASK changes the feedback on every
  step, and in Auto its CLK clocks the register.

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Selector: Auto (CLOCK jack, else the MASK bus CLK, else Internal), CLOCK jack, Bus CLK (the MASK bus), Internal, Engine rate. |
| SENSE | Selector: XOR or XNOR (default). |
| RATE | Internal clock, 0.125–2048 Hz, default 8 Hz. Used only when Internal owns the clock. |
| MASK | Integer 00–FF, default B8: bit *k* taps stage *k* + 1. Replaced by the MASK bus when it is patched. |
| THRESH | INJECT's comparator threshold, −5 … +5 V, default +1.2 V, the hardware's. |
| SEED | Word 00–FF, default 00. RESET and LOAD load it. |
| LOAD | Loads SEED, restarts the internal clock phase and publishes the word. |
| HOT | Latching button with a light. See [Levels](#levels). |
| W 1–8 | Stage weights, −100 … +100 %, default +100 %, with a ±2 % dead zone at the centre. |
| CLOCK | Monophonic clock; rising at 1 V, falling at 0.1 V. |
| RESET | Monophonic trigger, same thresholds; loads SEED on its rising edge. |
| INJECT | Any signal; a comparator at THRESH with ±0.05 V hysteresis. Unpatched, it injects nothing. Its light shows the comparator. |
| MASK BUS | A byte bus: its data lines are the mask, sampled on this module's edge; its CLK is the Bus CLK source. |
| W CV | Polyphonic: channel *k* adds 1 (100 %) per 5 V to W *k* + 1, and the weight is clamped to ±1. Missing channels add nothing; a mono cable moves W 1 only. |
| BUS OUT | The register: 9 channels with a strobe on each step, or 8 at Engine rate. |
| CLK OUT | The CLK channel of BUS OUT as a gate. |
| GATE | Stage 1, the newest bit, 0/10 V. |
| CV | The weighted sum of the set stages. |
| AUDIO | The weighted sum with set stages as +1 and clear stages as −1. |

Eight lights show the stages, stage 1 at the left. The readouts show the word
and the mask in use, in hexadecimal. Stages are numbered 1–8 on the panel and
0–7 on the bus: stage 1 is bit 0.

## The register

On each acquisition edge:

```
new  = parity(word AND mask) XOR sense XOR inject
word = (word << 1 | new) AND FF
```

- *sense* is 0 for XOR and 1 for XNOR; *inject* is INJECT's comparator at the
  edge.
- MASK 00 has no feedback: the register is a delay line for INJECT (inverted
  under XNOR).
- **Lockup.** Under XOR the word 00 maps to itself, under XNOR FF does. Without
  INJECT the register stays there until RESET, LOAD or another mask moves it.
  The defaults, B8 under XNOR from 00, run a full 255-step cycle at once.
- **Maximal masks.** Sixteen masks give a 255-step cycle from any word but the
  lockup: 8E 95 96 A6 AF B1 B2 B4 B8 C3 C6 D4 E1 E7 F3 FA. Masks tapping stage
  8 give 27 different cycle lengths from 01 under XOR, 8 to 255.
- **The six-stage original.** The hardware has six stages, each with a switch
  for Q, off or not-Q. That is MASK within 3F, with SENSE XNOR when an odd
  number of switches are on not-Q, and INJECT its SIGNAL input: the first six
  stages then match it bit for bit, and stages 7 and 8 only delay. Six masks
  give its maximal 63-step cycle: 21 2D 30 33 36 39.
- The mask in use is MASK, or the MASK bus's data lines as sampled on the
  latest edge while it is patched. Unpatching it returns to MASK at once.

## Levels

With *b* the stage bits and *w* the weights (knob plus W CV, clamped to ±1):

| Output | Value |
|---|---|
| CV | 1.25 V × Σ *w*·*b*, bits as 0/1: at most ±10 V |
| AUDIO | 1.25 V × Σ *w*·(2*b* − 1), bits as ±1: at most ±10 V |

With HOT on, a stage is worth 6 V instead of 1.25 V and both outputs clip at
±10.5 V, as the hardware's 12 V logic into its summer clips with two or three
stages set. The outputs change on each step and at once when a weight or W CV
changes: W CV at audio rate modulates each stage's level.

What the weights do at clock rate *f*: all eight +1 is a moving sum, a lowpass
with nulls at multiples of *f*/8; alternating signs is a highpass; one nonzero
weight is a single bit, delayed. A lowpass after AUDIO removes the stepping
when the clock is at audio rate.

## Clock, reset and strobe timing

Clock ownership is the shared `src/engine/ClockOwner.hpp`, as on Counter: Auto
selection by connection, publication of the held state before acquisition after
a fresh start, Initialize or restore, one-sample strobes with a mandatory low
sample, RESET priority over a simultaneous edge, the seed published after RESET
or LOAD with acquisition suppressed for that strobe and the low sample after it.
Engine rate steps every sample and emits no clock channel. Each cable hop costs
one engine sample.

The hardware's RST holds the register at 00 for as long as it is high;
Parallel Sum's RESET loads SEED on its rising edge.

## Saving and reset

Rack saves the controls and the word, as `"word"`, bounded to 00–FF. Reload
publishes the saved word; it does not resume the fractional internal clock
phase. Rack's Initialize restores the defaults (MASK B8, XNOR, HOT off, every
weight +100 %, THRESH +1.2 V, SEED 00) and word 00.

## First patch

1. **Parallel Sum** with nothing patched: Internal at 8 Hz. The stage lights
   shift left to right and CV OUT steps through a 255-step pattern.
2. CV OUT → an oscillator's V/OCT, and set a few weights to zero: fewer
   levels, a different melody, the same rhythm of change.
3. A VCO square at a few hundred hertz → CLOCK, AUDIO → a lowpass filter →
   your mixer: a buzz whose timbre the weights set. Alternate the weights' signs
   for a thinner sound; set MASK 8E or C3 for other cycles of the same length.
4. MASK 88, then LOAD: from 00 a 12-step cycle, a tone at the clock / 12.
   MASK 82 gives 30 steps from 00. Short cycles depend on the word they start
   from; maximal masks do not.
5. GATE → INJECT: stage 1 joins the feedback, and the loop changes.
6. A **Counter** BUS OUT → MASK BUS, SOURCE Auto: the register steps on the
   Counter's clock, under a new mask each time.

## Validation

Portable tests (`tests/engine_test.cpp`): the step against a tap-by-tap
reference for every word, mask, sense and inject; the sixteen maximal masks and
no others, with their lockups; the six six-stage maximal masks; the 27 cycle
lengths; all 729 settings of the original's six three-way switches with its
SIGNAL input, against MASK, SENSE and INJECT; CV and AUDIO equal to the direct
convolution of the register's serial stream under random weights and W CV;
levels and HOT clipping; the weight dead zone and W CV clamping; the INJECT
comparator's threshold, hysteresis and range; the MASK bus sampled on this
module's edge and clocking it in Auto; RESET priority, strobes and the
published words under random clocks, resets and masks; the internal clock at
44.1/48/96 kHz.

Rack API tests (`tests/module_test.cpp`): real ports and lights, the default
cycle at Engine rate with CV, AUDIO and GATE, W CV mono and polyphonic, HOT,
the MASK bus, INJECT, GATE → INJECT through a one-sample cable equal to tapping
stage 1, persistence with bounded restore, Initialize; a Counter → MASK BUS
chain clocking Parallel Sum in random module order, identical at
44.1/48/96 kHz.

Software tests do not establish physical clock transport.

## First sample after Initialize or restore

A fresh instance and Initialize start at 00; restore starts from the saved word.
The first sample publishes that word without acquiring from an already-high
clock. A RESET or LOAD already high on that sample takes priority and loads
SEED instead; holding it high does not reload or block later clock edges.
MASK BUS supplies raw lines on acquisition, not on initial publication.
INJECT is a comparator level, effective immediately when above its threshold
plus hysteresis, but affects the register only on acquisition.
