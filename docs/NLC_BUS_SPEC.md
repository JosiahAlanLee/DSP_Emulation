# NLC-derived bus modules: five ByteBusLab modules, spec v0.1

Status: **draft for ratification; nothing implemented.** Five new
[ByteBusLab](../vcv/bytebus) modules, each built around the central idea of
one Nonlinearcircuits module and rebuilt to the
[Harriot byte bus](BYTE_BUS_SPEC.md) v0.1 rules. They are Harriot modules,
not hardware panels: controls, jacks and ranges follow ByteBusLab conventions,
and each module adds bus ports where its circuit already holds a binary word.

Working names are placeholders; names, slugs and glyphs are the owner's to
change (§12, item 1).

| Working name | From (Nonlinearcircuits) | Core idea kept | Kind |
|---|---|---|---|
| **Wheel** | Pseudo | a 24-stage pseudo-random register feeding a loop register, read through binary and bell-curve DACs | latching sender |
| **Parallel Sum** | Burhans Timbre | a PN register whose stages are summed through per-stage weights: an FIR filter on the bit stream | latching |
| **Roulette** | Wangernumb | a PLL frequency tracker driving a 12-stage divider, and a gate that spins the divider and lets it coast to a stop | sender with its own VCO |
| **Lattice** | Bindubba | two 4-bit up/down counters on separate clocks addressing a 4×4 grid, each able to jump to the other's hidden bits | latching, two clocks |
| **Fan** | 32:1 | a 5-bit address routing one signal to 32 places or 32 to one | latching or transparent |

The owner has a Wangernumb and a Bindubba; §9 lists the measurements that
settle what this draft could not.

## 1. Purpose

ByteBusLab covers the Harriot role set: conversion, logic, lookup, counting,
delay, switching. These five add what Nonlinearcircuits' CMOS modules do with
binary state that the role set does not: long pseudo-random registers with
analog readouts (Wheel), shift-register timbre filtering (Parallel Sum), a
pitch-tracked counter as a bus source (Roulette), two-axis counting with
cross-coupled jumps (Lattice), and wide addressed routing (Fan).

## 2. Sources and provenance

### 2.1 Policy

- The owner chose, for this family, to let the specs draw on Nonlinearcircuits'
  public build documentation as well as module descriptions, demonstrations and
  measurements of the owner's own units. This differs from the Harriot
  clean-room rule ([BYTE_BUS_SPEC §2](BYTE_BUS_SPEC.md#2-clean-room-rule)),
  which concerns the Xaoc subsystem and stays as it is.
- Module names are ours; Nonlinearcircuits module names, panel art and layouts
  are not reused. Andrew Fitch designed the originals; each module's manual
  credits him and the published work his designs cite.
- Where a build document or measurement later contradicts this draft about
  hardware behaviour, the document or measurement wins and the draft is
  revised before implementation. Original Harriot behaviour (marked *original*
  below) is unaffected by that rule.
- Marking used below: **documented** means a source states it; **inferred**
  means reasoned from documented facts (the reasoning is given); **original**
  means a design decision made here with no hardware claim.

### 2.2 What was reached

The build PDFs are linked from each module page, but four of the five could not
be retrieved for this draft (their hosting redirects were not reachable from
the drafting environment). Only Bindubba's build document was read, and it has
no schematic. Every item that a schematic would settle is listed under *Not
established* in its module section and in §12.

| Id | Source | Used for |
|---|---|---|
| P1 | [Pseudo product page](https://www.nonlinearcircuits.com/modules/p/pseudo) | Wheel |
| B1 | [Burhans Timbre product page](https://www.nonlinearcircuits.com/modules/p/burhans) | Parallel Sum |
| B2 | R. W. Burhans, "Pseudo-Noise Timbre Generators", JAES 20(3), 1972, [abstract](https://aes2.org/publications/elibrary-page/?id=2086) | Parallel Sum |
| B3 | J. Paradiso, [notes on his modular system](https://paradiso.media.mit.edu/ModularSynthsVoltageConnect-17-ReallyFinal.pdf): Burhans-inspired registers with selectable XOR feedback per stage | Parallel Sum (analogy only) |
| W1 | [Wangernumb product page](https://www.nonlinearcircuits.com/modules/p/wangernumb) | Roulette |
| W2 | A. Fitch, ["Wangernumb revisited"](https://www.nonlinearcircuits.com/blog/2016/10/wangernumb-revisited.html), 2016 | Roulette |
| W3 | J. Dent, [Wangernumb build notes](https://djjondent.blogspot.com/2017/08/wagernumb-nlc-build-notes.html), 2017 | Roulette |
| W4 | [synthCube listing](https://synthcube.com/nlc1061-wangernumb-white-nlc/) (frequency tracker for chords, after Lancaster's *CMOS Cookbook*) | Roulette |
| D1 | [Bindubba product page](https://www.nonlinearcircuits.com/modules/p/bindubba) | Lattice |
| D2 | [Bindubba build & BOM](https://static1.squarespace.com/static/5e6e99e8bd2f8a6de8454feb/t/5e78014e28dc473dd31e6512/1584922974167/bindubba_sequencer_build_and_bom.pdf) | Lattice |
| F1 | [32:1 product page](https://www.nonlinearcircuits.com/modules/p/32to1) | Fan |
| F2 | J. Dent, [32:1 build notes](https://djjondent.blogspot.com/2017/07/nlc-321-build-notes.html), 2017 | Fan |
| C1 | [CD4029B datasheet](https://led.det.polito.it/datasheet/cd4029.pdf) | Lattice, Fan |
| C2 | [DG508A datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/dg508a-dg509a.pdf) | Fan |

Not reached, wanted: the Pseudo, Burhans Timbre, Wangernumb and 32:1 build
PDFs; Electronotes 76 (the ENS-76 "Random Source" article Pseudo is based on);
the full Burhans paper.

## 3. Rules shared by all five

- **Harriot v0.1 as ratified.** Buses are 8 data channels, or 9 with CLK;
  0 V / 10 V bits; Schmitt receivers at 1.0 V / 0.1 V; LSB on channel 0. No
  module here needs a v0.1 change.
- **State wider than a byte** is exposed without wide words
  ([§12 of the bus spec](BYTE_BUS_SPEC.md#12-extensions-not-in-v01) leaves
  those for later): Wheel reads 8 of 24 stages through a window, Roulette
  emits its 12-bit count as two overlapping bytes, Lattice packs two nibbles,
  Fan uses 5 bits of a word.
- **Clock ownership.** Every latching module here uses the shared
  `src/engine/ClockOwner.hpp`, so SOURCE, RATE (0.125–2048 Hz), seed
  publication, one-sample strobes, the mandatory low sample and RESET priority
  are exactly the Clocked Register's
  ([MANUAL](../vcv/bytebus/docs/MANUAL.md#clock-ownership)). A module with no
  bus input offers no Bus CLK choice, as Counter 2 does. Exceptions are stated
  per module: Roulette's internal clock is its own VCO, and Lattice has a
  second clock.
- **Events closer than a strobe allows.** Where a module has more than one
  source of change (Lattice's two clocks and jumps), a
  change that lands on a sample whose CLK must be low is applied at once and
  its strobe published on the next sample, carrying the word as it then stands.
  No change is lost; a clocked reader may see two changes as one word.
- **Determinism.** Every pseudo-random sequence here is a pure function of the
  seed and the edge count. Nothing uses a free-running random generator.
- **Saving.** Rack saves the controls and the module's binary state; reload
  publishes the saved word and does not resume a fractional clock phase. Rack's
  Initialize restores default controls and the documented reset state.
- **Rates.** The internal RATE ceiling stays at 2048 Hz, the ByteBusLab
  convention. Tone use (Wheel, Parallel Sum, Fan) runs from CLOCK fed by an
  oscillator, up to half the engine rate, or from Engine rate. Pseudo itself
  needs an external clock to run (P1).

## 4. Wheel (from Pseudo)

### 4.1 What the sources establish

All from P1 unless noted.

- **Documented.** Two 24-stage shift registers. The first is moved by the
  incoming clock and has modulo-2 feedback, giving a sequence of over 16.7
  million steps, about 15 minutes at an 18 kHz clock. The second, the loop
  section, repeats after 8, 16 or 24 stages, or does not loop (noise); a gate
  or trigger, or toggling a switch, refreshes the loop. An external input
  resets the "capture wheel".
- **Documented.** Outputs: U1 and U2 are binary-weighted, DAC-like, from six
  stages (U1 spread across the 24, U2 six consecutive); G1 and G2 have a
  Gaussian distribution (G1 spread, G2 consecutive) and often look like a
  slightly glitchy sine; a Loop gate with 8-, 16- or 24-step patterns that
  reset with the CVs; a Pseudo Random gate not connected to the loop section.
  CMOS rather than the TTL of Electronotes 76; EN76's probability switches are
  dropped; an external clock is required.
- **Inferred.** 2^24 − 1 = 16,777,215 steps and 16,777,215 / 18,000 Hz ≈ 932 s
  match a maximal-length 24-bit register. No primitive trinomial of degree 24
  exists (checked: §10), so the feedback XORs at least four stages.
- **Inferred.** Equal-weight summing of bits gives a binomial distribution,
  the usual way to get a bell curve from a register; summing consecutive stages
  changes by at most one level per clock, which is the "glitchy sine".
- **Inferred, moderately strong.** The CVs read the loop register: the Loop
  gate resets "along with the CVs", and the Pseudo Random gate is singled out
  as the one output not connected to the loop section.

### 4.2 Design

**Registers.** R1 and R2 are 24-bit; stage 0 holds the newest bit.

- R1 (*original taps*): Fibonacci feedback from stages 23, 22, 21 and 16
  (the polynomial x^24 + x^23 + x^22 + x^17 + 1), period 2^24 − 1. With the
  *original* zero-state escape, the feedback bit is inverted whenever stages
  0–22 are all zero, which inserts the all-zero state and makes the period
  exactly 2^24 = 16,777,216, every 24-bit state visited once. No seed can lock
  it. (Checked: §10.)
- R2 shifts on the same edges. Its input bit is R2's stage LENGTH − 1 while
  LENGTH is 1–24 (a ring of LENGTH stages; stages past the ring carry delayed
  copies of it), and R1's stage 23, the bit leaving R1, while LENGTH is Off.
  Off therefore makes R1 and R2 one 48-stage line (*original*: the hardware's
  noise mode is documented, its wiring is not).

**Capture.** A rising edge on CAPTURE, or the CAPTURE button, arms a capture.
On the next acquisition edge R1 shifts and then R2 is set to R1, all 24
stages, so the loop restarts with fresh material on the beat. A capture is a
parallel copy (*original*; serial capture over LENGTH edges is the alternative,
§12).

**Reset.** RESET or LOAD sets R1 to SEED × 0x010101 (the seed byte in each of
its three bytes; 00 is valid because of the escape) and R2 to the same value,
then publishes, per ClockOwner.

**Reading.** Both buses read 8 stages through one window:
`bit k = stage (OFFSET + STRIDE · k) mod 24`, k = 0…7. STRIDE 1 is the
consecutive read (U2/G2), STRIDE 3 the spread read covering all 24 stages
(U1/G1); STRIDE 2 is in between. The window is 8 stages, not the hardware's 6,
so it is one bus word.

| Output | Value |
|---|---|
| LOOP BUS | R2 through the window: 9 channels, CLK on each acquisition edge |
| FREE BUS | R1 through the same window |
| U OUT | LOOP word as a DAC: `(n − 128) × 5/128` V, as Converter's ±5 V range |
| G OUT | bits of the LOOP word counted with equal weight: `(popcount − 4) × 1.25` V, nine levels −5 to +5 V, probabilities 1, 8, 28, 56, 70, 56, 28, 8, 1 in 256 for a random word |
| LOOP GATE | R2 stage 0, 0/10 V |
| NOISE GATE | R1 stage 0 (the newest feedback bit), 0/10 V |
| CLK OUT | the word-clock strobes |

Through a fast CLOCK, LENGTH *L* makes a periodic waveform of pitch
`f_clock / L` whose timbre is the loop's contents; a capture changes the timbre
without changing the pitch.

### 4.3 Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Auto (CLOCK jack, else Internal), CLOCK jack, Internal, Engine rate |
| RATE | Internal clock, 0.125–2048 Hz, default 8 Hz |
| LENGTH | Off, 1–24; default 16 |
| OFFSET | Integer 0–23, default 0 |
| STRIDE | 1, 2 or 3; default 1 |
| SEED, LOAD | Seed byte 0–255 and its load button |
| CAPTURE | Button; arms a capture |
| CLOCK, RESET, CAPTURE (inputs) | Monophonic, 1.0 V / 0.1 V thresholds |
| Outputs | as the table above |

About 14 HP. Hex display shows the LOOP word.

### 4.4 Not established

Which stages the hardware taps for feedback and for each DAC; the DAC
resistor weights, offset and gain; whether R2 is fed from R1's first or last
stage, or by a selector; what the "capture wheel" is electrically, and whether
capture is serial or parallel; which stages drive the two gates; whether the
feedback is XOR or XNOR. The Pseudo build PDF would settle all of these. None
is needed for the Harriot design above; each would change only a default.

## 5. Parallel Sum (from Burhans Timbre)

### 5.1 What the sources establish

- **Documented (B1).** Three uses: timbre generator, CV pattern sequencer,
  controllable pseudo-random noise. It is "not a 6 stage sequencer", since most
  settings make more than one channel active at once; the CV output is the sum
  of the voltages set by the pots of the active stages. Switches with a
  centre-off position allow extremely long patterns. A signal input changes and
  randomises patterns on the fly; a gate output fed back into it makes shorter
  loops that the switches still alter. As a timbre generator it adds harmonics
  to sine waves, or steps in and out of pseudo-random noise as signal and clock
  are modulated.
- **Documented (B2).** A pseudo-noise generator's serial bit stream has high
  harmonic content, which can be filtered by direct parallel sum selection from
  the register's outputs, giving a variable-timbre element.
- **Analogy (B3).** Paradiso's Burhans-inspired sequencers select XOR feedback
  at each stage and modulate the sequence by XORing external bits into the
  feedback.
- **Inferred.** Stage *k* of a shift register holds the bit that entered *k*
  clocks earlier, so a weighted sum of stages is a finite impulse response
  filter on the serial stream: `y(n) = Σ w_k · x(n − k)`. That is what makes
  the parallel sum a timbre control: the weights are filter coefficients at the
  clock rate.

### 5.2 Design

**Register.** Eight stages, one bus word; stage 0 newest. On each acquisition
edge:

```
new  = parity(word AND MASK) XOR inject
word = (word << 1 | new) AND FF
```

- MASK selects the feedback stages (knob 00–FF, or the MASK bus when patched,
  its raw data lines reclocked on this module's edge as Counter reclocks
  BUS IN). MASK 00 is no feedback: the register is a delay line for the
  injected bits.
- Sixteen masks give the maximal period of 255 from any nonzero word: 8E 95 96
  A6 AF B1 B2 B4 B8 C3 C6 D4 E1 E7 F3 FA. Over the 128 masks that tap stage 7,
  the cycles reached from 01 have 27 different lengths, from 8 to 255; the other
  masks behave as shorter registers. (Checked: §10.) Default MASK B8, taps
  8, 6, 5, 4 in 1-based numbering.
- No zero escape (*original*, deliberately unlike Wheel): word 00 with no
  injection stays 00. A zero escape would turn MASK 00 into a walking bit.
  Default SEED 01.
- `inject` is INJECT through a comparator at THRESHOLD (−5…+5 V, default 0 V,
  ±25 mV hysteresis), sampled on the edge. Unpatched, it is 0. A comparator at
  a set level, rather than the bus thresholds, lets a sine go in directly
  (B1's "adds harmonics to sine waves").

**Weights.** W0–W7, one per stage, −1…+1 with centre off (*original*: the
hardware's three-position switches are not understood, §5.4; a bipolar knob
with a centre detent covers off, add and subtract continuously). W CV is a
polyphonic input: channel *k* adds `V/5` to W*k*; the sum is clamped to ±1.

| Output | Value |
|---|---|
| BUS OUT | the register word, 9 channels, CLK on each edge |
| CV OUT | `1.25 V × Σ W_k · b_k`, bits as 0/1: a stepped CV, ±10 V at most |
| AUDIO OUT | `1.25 V × Σ W_k · (2b_k − 1)`, bits as ±1: zero-mean for a balanced stream |
| GATE OUT | stage 0, the newest bit, 0/10 V |
| CLK OUT | word-clock strobes |

What the weights do, at clock rate *f*: all eight +1 is a moving sum, a lowpass
with nulls at multiples of *f*/8; alternating signs is a highpass; one
nonzero weight is a single delayed bit. Both outputs hold between edges, so a
lowpass filter after them removes the stepping when the clock is audio rate.
GATE OUT patched into INJECT adds stage 0 to the feedback, changing the
effective mask: the "shorter loops" of B1.

### 5.3 Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Auto (CLOCK jack, else Bus CLK of MASK, else Internal), CLOCK jack, Bus CLK, Internal, Engine rate |
| RATE | 0.125–2048 Hz, default 8 Hz |
| MASK | Integer 00–FF, default B8; replaced by the MASK bus when patched |
| THRESHOLD | −5…+5 V, default 0 V |
| W0–W7 | −1…+1, centre detent; default all +1 |
| SEED, LOAD | default 01 |
| CLOCK, RESET, INJECT, MASK (bus), W CV (poly) | inputs |
| Outputs | as above |

About 16 HP. Hex displays show the word and the mask.

### 5.4 Not established

The hardware's stage count (the "not a 6 stage sequencer" sentence suggests
six); what each position of the three-position switches does (feedback tap,
output selection, inversion, or a combination); how the signal input enters
the register; the number and sources of gate outputs; voltage ranges. The
Burhans Timbre build PDF would settle these. If the switches turn out to
select per-stage feedback taps, MASK already expresses that; if they select
inversion, a sign on the weights already expresses that.

## 6. Roulette (from Wangernumb)

### 6.1 What the sources establish

- **Documented.** With the Track input used, the phase-locked loop is a
  frequency tracker; the VCO Range CV and the filter pot decide how well, or
  how poorly, it tracks (W1). The 12 outputs are divisions of the tracker's
  output frequency, down to /4096 (W1, W2). The Divide input disconnects the
  PLL from the 12 outputs and makes them a 12-stage divider of that input, the
  PLL then free as a barely controlled VCO (W2).
- **Documented (W2).** Roulette wants a CV or gate: while it is high the
  outputs spin; when it returns to zero they halt, giving a random combination
  of gates, plus bursts while spinning. The range pot and Range CV set the rate
  of spin. The Range CV drives a vactrol that sets the PLL's frequency range.
- **Documented (W3).** A CD4046 PLL and a vactrol on the board; two pots
  (inferred to be Range and Filter); 17 jacks; 12 gate outputs of about 7 V;
  a VCO output, silent until audio or CV reaches Track or Roulette.
- **Documented (W4).** Sold as a frequency tracker for generating chords,
  following the concepts in Don Lancaster's *CMOS Cookbook*.
- **Inferred.** The silent VCO with no input fits the 4046's edge-sensitive
  comparator (PC2), which drives the VCO down when there are no input edges,
  with the VCO's minimum-frequency resistor left out, so the minimum is zero.
  The XOR comparator (PC1) would park the VCO mid-range instead. This is the
  main item for measurement (§9.1).
- **Inferred.** Four inputs, twelve gates and one VCO output make the
  documented 17 jacks, which leaves no jack for a feedback-divider selector,
  so the VCO most likely runs at the input frequency (a tracker, ×1), not a
  multiple of it.

### 6.2 Design

**The loop.** `vc`, 0…1, is the loop-filter capacitor voltage; the VCO runs at
`f = F_MAX × vc`, stopping below `vc = 0.01` (*provisional*). Each engine
sample, `vc` moves toward a demand with time constant FILTER; the first row
that applies sets the demand:

| Condition | Demand |
|---|---|
| ROULETTE high | 1: spin at F_MAX (*inferred mechanism*, §9.1) |
| TRACK patched, Edge comparator | 1 while an input edge leads the VCO edge, 0 while the VCO leads; while neither is pending, `vc` holds (the 4046 PC2's three states) |
| TRACK patched, XOR comparator | the XOR of input and VCO squares (PC1) |
| neither | 0: the wheel coasts down |

- TRACK goes through a DC blocker (about 10 Hz) and a comparator at 0 V with
  ±25 mV hysteresis, as the 4046's self-biased input would act on audio.
- When ROULETTE falls, `vc` decays with FILTER's time constant, so the counter
  slows and stops after roughly `4.6 × FILTER` (the time to fall to 0.01): a
  wheel coasting to rest (*inferred*, the main Roulette measurement).
- F_MAX: RANGE, 0.1 Hz–20 kHz on a log scale, default 2 kHz, times
  `2^(RANGE CV)` (1 V per octave). The CV path passes a vactrol-like lag,
  5 ms rising and 50 ms falling (*provisional*, §9.1).
- VCO OUT: the VCO square, ±5 V, band-limited (polyBLEP). The phase
  accumulator that makes it also clocks the counter, so they never disagree.

**The counter.** Twelve bits, binary, counting each rising edge of the
selected clock:

- SOURCE Auto: DIVIDE if patched, else the VCO. The VCO is passed to
  ClockOwner as its internal clock, with RATE replaced by the VCO frequency
  each sample; ClockOwner caps it at half the engine rate, so every count gets
  a strobe.
- Bit *k* divides by 2^(k + 1): /2 on bit 0, /4096 on bit 11.
- RESET sets the count to 0, restarts the VCO's phase (ClockOwner restarts its
  internal clock) and publishes the count.
- There is no RATE knob or Engine choice: the VCO is the internal clock.

| Output | Value |
|---|---|
| LO BUS | count bits 0–7 (/2 … /256), 9 channels, CLK on each count |
| HI BUS | count bits 4–11 (/32 … /4096), same clock; bus bits 0–3 of HI are bits 4–7 of LO |
| RESULT BUS | LO's word at the moment the VCO stops, published once per stop with one strobe (*original*): one random byte per spin, on a clock that marks the wheel coming to rest |
| VCO OUT | as above |
| CLK OUT | count strobes |

Twelve mono gates are LO and HI through Core Split; HI's overlap means each
bus alone is a contiguous eight-octave divider.

### 6.3 Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Auto (DIVIDE jack, else VCO), DIVIDE jack, VCO |
| RANGE | F_MAX, 0.1 Hz–20 kHz log, default 2 kHz |
| FILTER | loop time constant, 0.1 ms–2 s log, default 20 ms |
| COMPARATOR | Edge (default) or XOR |
| TRACK | audio input |
| ROULETTE | gate, 1.0 V / 0.1 V |
| DIVIDE | clock, 1.0 V / 0.1 V |
| RANGE CV | 1 V/octave on F_MAX, through the lag |
| RESET | trigger |
| Outputs | as above |

About 14 HP. A frequency readout shows the VCO; LEDs show the twelve bits.

### 6.4 Not established

Which 4046 comparator the hardware uses; the loop-filter components; whether
Roulette drives the loop filter, the VCO inhibit pin or the signal input;
whether the spin coasts after the gate falls; the counter chip (a CD4040 is
inferred from "12 stage … 1/4096" but not named); the vactrol's response; the
VCO's range at the pot's ends. §9.1 settles most of these on the owner's unit;
the build PDF would settle the rest.

## 7. Lattice (from Bindubba)

### 7.1 What the sources establish

- **Documented (D2).** Two CD4029 up/down counters, two CD4052 dual
  4-channel muxes, four CD4081 quad AND gates (sixteen AND gates; one per
  stage is inferred), eighteen 100 k pots, two vactrols, 29 jacks. A 16-stage
  sequencer arranged in a 4 × 4 grid, with two clock inputs for horizontal and
  vertical stepping, two direction inputs for forward and backward stepping,
  and two jump inputs that move the step to a position depending on the 3rd
  and 4th bits of the other clock's count; semi-random. Sixteen gate outputs,
  one per stage; two identical CV outputs and an inverted one; two slewed
  outputs, initially the same as the first CV output, each slewed by a pot and
  a CV, more CV meaning less slew.
- **Documented (D1).** A reboot of the earlier Bindubba 3, designed to be
  used with the Divide & Conquer clock divider.
- **Documented (C1).** The CD4029's preset enable is asynchronous, and the
  preset signal must be low before a rising clock edge for that edge to count.
  Counting occurs on the rising clock edge, with a setup time for the up/down
  input.
- **Inferred.** An asynchronous load held high keeps the outputs equal to the
  jam inputs, and clock edges do nothing meanwhile; the datasheet does not say
  so in as many words.
- **Inferred.** Each counter's two low bits are its grid coordinate, which is
  why the 3rd and 4th bits are free to be the jump source: they are state the
  panel does not show.
- **Inferred.** Jump drives the jumping counter's preset enable, with its first
  two jam inputs wired to the other counter's 3rd and 4th bits.
- **Inferred.** The 29 jacks are 6 stepping inputs, 2 slew CVs, 2 slew outputs,
  3 CV outputs and 16 gates; no reset.

### 7.2 Design

**State.** H and V, each 4 bits, 0–15, binary. The step is
`s = 4 × (V AND 3) + (H AND 3)`, 0–15: V is the row, H the column.

**Clocks.**

- H advances on H CLOCK, or on the internal RATE clock when H CLOCK is
  unpatched (ClockOwner, SOURCE as Counter's without Bus CLK).
- V advances on V CLOCK; unpatched, on H's position wrap, the edge where H's
  low two bits go from 3 to 0 counting up or 0 to 3 counting down
  (*original*). With nothing patched but a clock, Lattice is a plain 16-step
  sequence 0, 1, … 15.
- DIR H and DIR V: while high, that counter counts down (*original
  polarity*). A counter wraps modulo 16.

**Jumps.** JUMP H loads H's position bits from V's bits 2–3; H's own bits 2–3
are kept (*original*: whether the hardware's third and fourth jam inputs take
something else is unknown, §7.4). JUMP V does the same with the roles
swapped. Both evaluate from the state before the jump, so simultaneous jumps
are well defined.

| JUMP mode | Behavior |
|---|---|
| Level (default; how an asynchronous CD4029 preset is inferred to behave) | While high, the position follows its source continuously, and that counter's clock edges are ignored; when it falls, counting resumes from there |
| Edge | Loads once on the rising edge |

BUS IN (*original*): when patched, JUMP H loads all of H from bits 0–3 of
BUS IN's raw word and JUMP V all of V from bits 4–7, the same layout as
BUS OUT. BUS OUT → Lookup → BUS IN jumps to a table function of the state.

**Outputs.**

| Output | Value |
|---|---|
| CV OUT | step knob *s*, in the CV RANGE |
| INV OUT | −CV OUT |
| SLEW A, SLEW B | CV OUT through a one-pole lag each: time constant from its knob (0.1 ms–2 s, log) multiplied by `2^(−CV)`, so each volt halves it; the CV path through a vactrol-like lag, 5 ms rising, 50 ms falling (*provisional*) |
| BUS OUT | `V × 16 + H`: the whole state, hidden bits included; 9 channels, a strobe on every change (§3 for close events) |
| STEPS 1–8, STEPS 9–16 | one-hot gates, 0/10 V, 8 channels each, unclocked |
| CLK OUT | the state strobes |

RESET or LOAD sets H to SEED bits 0–3 and V to bits 4–7 and publishes.

### 7.3 Controls and ports

| Item | Behavior |
|---|---|
| STEP 1–16 | sixteen knobs, 0…1 of CV RANGE, in a 4 × 4 grid |
| CV RANGE | 0–10 V (default), 0–5 V, ±5 V |
| SOURCE | H's clock: Auto (H CLOCK jack, else Internal), H CLOCK jack, Internal, Engine rate |
| RATE | internal H clock, 0.125–2048 Hz, default 8 Hz |
| JUMP mode | Level, Edge |
| SLEW A, SLEW B | knobs |
| SEED, LOAD | state byte |
| H CLOCK, V CLOCK, DIR H, DIR V, JUMP H, JUMP V, RESET | inputs, 1.0 V / 0.1 V |
| BUS IN | jump targets |
| SLEW A CV, SLEW B CV | inputs |
| Outputs | as above |

About 26 HP. LEDs under each step knob; a hex display shows the state.

### 7.4 Not established

Binary or decade mode (binary is inferred from "2^2 and 2^3 counts");
which counter is rows; what the third and fourth jam inputs receive (zero,
the counter's own bits, the other's low bits); whether an edge detector sits
in front of preset enable; direction polarity; the CV and gate ranges; the
slew circuit's actual range and asymmetry. §9.2 settles all of these on the
owner's unit.

## 8. Fan (from 32:1)

### 8.1 What the sources establish

- **Documented (F1).** Splits one input into 32 outputs or switches 32 inputs
  into one; a reset gives lower counts; a direction control; with nothing in
  the in/out jack it works as a 32-stage counter. Out 1 gives an extra trigger
  on count 17, caused by a slight delay when switching the multiplexers'
  enable; NLC's note names the DG508.
- **Documented (F2).** One builder's 2017 kit used two DG506B 16-channel
  multiplexers and two CD4029 counters; five LEDs show the count in binary,
  and the designer confirmed it counts 0–31.
- **Inferred, from §10's skew model.** With two 16-channel muxes sharing
  address bits 0–3 and bit 4 selecting the chip through EN, a late bit 4 on the
  15 → 16 transition briefly selects address 0, so out 1 pulses on the 17th
  count, as documented. With four 8-channel muxes and decoded enables, the 17th
  count would glitch out 9 instead (and outs 1, 17 and 25 at other counts). The
  documented glitch therefore fits the two-chip build.

### 8.2 Design

**Address.** 5 bits, 0–31, shown 1–32 on the panel.

- With BUS IN unpatched, an internal counter (ClockOwner) counts on each
  edge: up, or down while DIR is high or the DIR switch is Down (the gate
  inverts the switch); wrapping at LENGTH, 1–32, so it visits 0 … LENGTH − 1.
  EOC pulses on each wrap. RESET loads SEED, 0–31.
- With BUS IN patched, the address is bits 0–4 of BUS IN's word at its sample
  instants, and Fan is transparent: it forwards BUS IN's clock and mode. LENGTH,
  DIR and the internal clock are unused (*original*: Counter or Lookup supply
  any order).

**Routing.** Rack ports are one-directional, so the hardware's single in/out
jack becomes COM IN and COM OUT, and the 32 jacks become two 16-channel
polyphonic cables per direction. Both directions run at once from one
address.

| Port | Role |
|---|---|
| COM IN | demux source, mono; unpatched, normalled to 10 V, which makes OUT 1–16 and OUT 17–32 one-hot gates (the hardware's counter mode) |
| OUT 1–16, OUT 17–32 | 16 channels each; the selected channel carries COM IN |
| IN 1–16, IN 17–32 | 16-channel polyphonic inputs; missing channels read 0 V |
| COM OUT | the selected input channel |

UNSELECTED (menu): **Zero** (default; an unselected DG50x output floats,
which a 100 k input reads as 0 V) or **Hold** (each output keeps the last value
it carried while selected).

**GLITCH** (option, off by default): on any address change that flips bit 4,
for one engine sample the selection uses the new bits 0–3 with the old bit 4.
Counting up, that is out 1 at the 16 → 17 step, the documented glitch, and out
17 at the wrap 32 → 1; counting down, out 32 at 17 → 16 and out 16 at
1 → 32. Only the first is documented; the others follow from the same model
and are unverified. The real glitch lasts about as long as the multiplexers'
enable and address switching, around a microsecond or less at ±15 V (C2); one
engine sample is the shortest Rack event.

| Output | Value |
|---|---|
| BUS OUT | the address word (bits 5–7 zero); 9 channels with CLK on each change from the internal counter or a clocked BUS IN, 8 when following an unclocked BUS IN |
| EOC | one-sample pulse on each counter wrap |
| CLK OUT | strobes |

### 8.3 Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Auto (CLOCK jack, else Internal), CLOCK jack, Internal, Engine rate (internal counter only) |
| RATE | 0.125–2048 Hz, default 8 Hz |
| LENGTH | 1–32, default 32 |
| DIR | switch Up/Down; DIR gate inverts it while high |
| SEED, LOAD | 0–31 |
| UNSELECTED | Zero, Hold |
| GLITCH | off/on |
| CLOCK, RESET, DIR, BUS IN, COM IN, IN 1–16, IN 17–32 | inputs |
| Outputs | as above |

About 16 HP. Five address LEDs and a two-digit readout.

### 8.4 Not established

Whether the shipping module uses two DG506 or four DG508; how reset sets
the count length on the hardware (an output patched to reset, as is usual
with CMOS counters, or a dedicated control); whether direction is a switch or
a gate; the counter-mode output level. The owner does not have a 32:1, so the
build PDF is the only route.

## 9. Measurement plan (the owner's Wangernumb and Bindubba)

Method: drive test clocks and gates from Rack through the ES-3, and record the
module's outputs through the ES-6 into Rack, so stimulus and response share a
clock and every edge is timed to the sample. A scope is fine for anything
marked *by eye*. Results go in `vcv/bytebus/tests/hardware/` as CSV with the
patch that produced them.

### 9.1 Wangernumb

1. **Comparator.** A 100 Hz square into Track. Record Track and VCO out: zero
   phase offset is PC2, a 90° offset PC1. Then set Range so the VCO cannot
   reach 300 Hz and feed 300 Hz: PC1 can lock at 100 Hz on the harmonic, PC2
   cannot.
2. **Silence.** Unpatch Track. Does the VCO stop, run slowly, or sit
   mid-range? (PC2 without a minimum-frequency resistor stops.)
3. **Ratio.** VCO out against Track at 100, 400 and 1600 Hz: ×1, or a
   multiple? Then /2 against VCO, and in Divide mode /2 against the Divide
   input.
4. **Roulette coast.** A 1 s gate into Roulette with Track unpatched; record
   the /2 output for 5 s after the gate falls. Does it keep changing after the
   gate ends (coasting), and does the coast time follow the filter pot? Repeat
   at three filter settings.
5. **Spin rate.** With Roulette held high, the VCO frequency at the range pot's
   ends and at 0, 2.5 and 5 V on Range CV.
6. **Vactrol.** Step Range CV 0 → 5 V → 0 with Roulette high; record VCO
   frequency over time for the rise and fall times.
7. **Lock time.** Track stepping 100 → 200 Hz at three filter settings: time
   to lock, overshoot (*by eye*).
8. **Levels.** Gate high level and VCO out level (the build notes report about
   7 V gates).

### 9.2 Bindubba

1. **Grid.** Clock H only, then V only: which LEDs light in which order; which
   counter is the row.
2. **Hidden bits and binary/decade.** The power-up state is unknown and there
   is no reset, so the test is relative: clock H once, pulse JUMP V, note V's
   new position; repeat for 32 H clocks. Binary counting with position ← H's
   bits 2–3 predicts a position that changes every 4 H clocks and repeats every
   16; decade counting repeats every 10.
3. **Level or edge.** Hold JUMP H high and clock V repeatedly: if H's LED
   follows V's hidden bits as they change, preset is level-sensitive.
4. **Upper jam inputs.** Probe H's hidden bits with JUMP V (V's position
   becomes them, as in 2), clock V once, pulse JUMP H, probe again. Kept: the
   same position as the first probe. Zeroed: position 0 every time. Copied
   from V's low bits: the position V had just before the jump. Some starting
   points make two answers coincide, so repeat from several.
5. **Direction.** Which level on DIR counts down.
6. **CV.** Out at the step pot's ends; whether the inverted output is −CV or a
   reflection about a level.
7. **Slew.** A full-scale step through each slew output at three pot settings,
   and at 0, 2.5 and 5 V slew CV: time constants rising and falling.
8. **Gates.** High level of the sixteen gates.

## 10. Testing

Arithmetic stated in this draft was run before writing it: the R1 taps give
2^24 − 1 from any nonzero seed; with the escape the cycle is 2^24, every state
visited once; none of the 23 degree-24 trinomials is maximal; the sixteen
maximal 8-bit masks and the 27 cycle lengths; the binomial counts; Lattice's
unpatched-V raster; both Fan skew models.

Each module then gets, as the existing ByteBusLab modules do:

- **Portable engine tests** (`tests/engine_test.cpp`).
  - Wheel: the full 2^24 cycle and state coverage; window reads for every
    OFFSET and STRIDE; each LENGTH's loop period; capture landing on the next
    edge; reset determinism; G's distribution over the cycle.
  - Parallel Sum: all 256 masks against a reference stepper; CV and AUDIO
    equal to direct convolution of the serial stream; inject; W CV clamping.
  - Roulette: count bits as exact divisions; lock to 50 Hz–5 kHz within a
    bounded number of cycles at mid FILTER; Edge comparator phase error near
    zero and XOR near 90°; coast to rest and one RESULT strobe per stop; VCO
    capped at half the engine rate with every count strobed; 44.1/48/96 kHz.
  - Lattice: all 256 states under every input combination against a reference
    transition function; level and edge jumps; simultaneous jumps; BUS IN
    jumps; the raster; strobe deferral under random clocks and jumps.
  - Fan: every address, LENGTH and direction; mux and demux routing; Hold;
    glitch pulses exactly on bit-4 changes; bus addressing.
  - All: strobe gaps and RESET priority under random clocks and resets.
- **Rack API tests** (`tests/module_test.cpp`): real ports and lights; JSON
  restore publishing the saved word; Initialize; patches with Counter, Lookup
  and Split through one-sample cables in random module order at 44.1/48/96 kHz.
- **Hardware comparison** (Roulette and Lattice): the §9 recordings replayed
  through the engine, with the comparisons committed as tests where the engine
  claims to match.

## 11. Delivery

- ByteBusLab, one minor version per module, each with a contract doc in
  `vcv/bytebus/docs/` that replaces its section here, an engine header, a
  module, tests, a panel from `tools/make_panels.py`, a row in the glyph table
  and plugin.json. ByteBusLab stays outside the release bundles.
- Proposed order: Fan (pure routing, smallest), Lattice (§9.2 first),
  Parallel Sum, Wheel, Roulette (§9.1 first; the PLL needs the most
  modelling).

## 12. Open decisions

1. **Names, slugs, glyphs.** Wheel, Parallel Sum, Roulette, Lattice and Fan
   are placeholders. Glyph proposals from the twelve marks: Wheel XI Reflexive
   loop (the loop feeds itself until a capture replaces it), Parallel Sum X
   Generative substrate, Roulette VI Regulation (the loop pulls the VCO to its
   input), Lattice XI Reflexive loop (each axis moves the other), Fan V
   Bottleneck (one of 32 gets through).
2. **Wheel's period.** 2^24 with the zero escape (proposed) or the maximal
   2^24 − 1 with a lockup state.
3. **Wheel's loop feed and capture.** R1's last stage into R2 when Off, making
   one 48-stage line (proposed), or R1's first stage, making the buses equal;
   parallel capture on the next edge (proposed) or serial over LENGTH edges.
4. **Parallel Sum's weights.** Continuous bipolar knobs (proposed) or a
   three-position switch per stage once the hardware's meaning is known.
5. **Roulette's loop model.** ROULETTE as a pull on the loop filter with
   coasting (proposed, pending §9.1 items 2 and 4); Edge comparator as default
   (pending item 1); ×1 tracking (pending item 3); the RESULT bus.
6. **Lattice's jumps.** Keep own bits 2–3 (proposed), level mode default
   (pending §9.2 items 3 and 4); DIR polarity; V normalled to H's position wrap.
7. **Fan's ports.** Two 16-channel cables per direction (proposed) or 32 mono
   jacks; GLITCH modelling all four predicted transitions (proposed) or only
   the documented one.
8. **Build order** (§11).
9. **Contacting Nonlinearcircuits** before these are published, as a
   courtesy; the names already avoid theirs.
