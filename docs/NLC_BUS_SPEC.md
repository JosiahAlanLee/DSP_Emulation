# NLC-derived bus modules: five ByteBusLab modules, spec v0.1

Status: **draft for ratification; nothing implemented.** Revised 2026-10-27
with the Burhans Timbre, Wangernumb, Bindubba and 32:1 build documents and
their schematics. Five new [ByteBusLab](../vcv/bytebus) modules, each built
around the central idea of one Nonlinearcircuits module and rebuilt to the
[Harriot byte bus](BYTE_BUS_SPEC.md) v0.1 rules. They are Harriot modules,
not hardware panels: controls, jacks and ranges follow ByteBusLab conventions,
and each module adds bus ports where its circuit already holds a binary word.
Where a schematic is available, the module's core behaves as the circuit does.

Working names are placeholders; names, slugs and glyphs are the owner's to
change (§12, item 1).

| Working name | From (Nonlinearcircuits) | Core idea kept | Kind |
|---|---|---|---|
| **Wheel** | Pseudo | a 24-stage pseudo-random register feeding a loop register, read through binary and bell-curve DACs | latching sender |
| **Parallel Sum** | Burhans Timbre | a register with an XOR feedback chain, each stage tapped as Q, off or not-Q, and its stages summed through per-stage weights: an FIR filter on the bit stream | latching |
| **Roulette** | Wangernumb | a 4046 PLL with its XOR comparator and a slow lead-lag filter driving a 12-stage divider; a gate that replaces the comparator, spins the divider and lets it coast to a stop | sender with its own VCO |
| **Lattice** | Bindubba | two 4-bit up/down counters on separate clocks addressing a 4×4 grid, each jump presetting one counter from its own upper bits and the other counter's position | latching, two clocks |
| **Fan** | 32:1 | a 5-bit address routing one signal to 32 places or 32 to one | latching or transparent |

The owner has a Wangernumb and a Bindubba; §9 lists the measurements that
settle what the documents leave open.

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
  are not reused, and the build documents are cited, not copied into the
  repository. Andrew Fitch designed the originals; each module's manual credits
  him and the published work his designs cite (Burhans; Driscoll and Burt).
- Where a build document or measurement contradicts this draft about hardware
  behaviour, the document or measurement wins and the draft is revised before
  implementation. Original Harriot behaviour (marked *original* below) is
  unaffected by that rule.
- Marking used below: **documented** means a source states or draws it;
  **inferred** means reasoned from documented facts (the reasoning is given);
  **original** means a design decision made here with no hardware claim.
- Supply and thresholds: all four schematics run the logic from +12 V and
  condition their clock, gate and signal inputs the same way, a 10 k / 100 k
  divider into a TL07x comparator against a 12 V × 10 k / 110 k = 1.09 V
  reference, so each such input switches at about **1.2 V at the jack**. That
  is "the hardware threshold" below. (Wangernumb's Roulette and Range CV are
  not comparator inputs; §6.1.)

### 2.2 Sources

| Id | Source | Used for |
|---|---|---|
| P1 | [Pseudo product page](https://www.nonlinearcircuits.com/modules/p/pseudo) | Wheel |
| B1 | [Burhans Timbre product page](https://www.nonlinearcircuits.com/modules/p/burhans) | Parallel Sum |
| B2 | R. W. Burhans, "Pseudo-Noise Timbre Generators", JAES 20(3), April 1972, pp. 174–184, [abstract](https://aes2.org/publications/elibrary-page/?id=2086) | Parallel Sum |
| B4 | [Burhans Timbre build & BOM](https://www.nonlinearcircuits.com/s/BURHANS-TIMBRE-Build-and-BOM.pdf), with schematic "Ralph Burhans PNTG" rev 1.0, and Burhans' Fig. 1 reproduced | Parallel Sum |
| W1 | [Wangernumb product page](https://www.nonlinearcircuits.com/modules/p/wangernumb) | Roulette |
| W2 | A. Fitch, ["Wangernumb revisited"](https://www.nonlinearcircuits.com/blog/2016/10/wangernumb-revisited.html), 2016 | Roulette |
| W3 | J. Dent, [Wangernumb build notes](https://djjondent.blogspot.com/2017/08/wagernumb-nlc-build-notes.html), 2017 | Roulette |
| W5 | [Wangernumb build & BOM](https://www.nonlinearcircuits.com/s/wangernumb_buildBOM.pdf), with schematic "wangernumb v5" rev 1.0 | Roulette |
| D1 | [Bindubba product page](https://www.nonlinearcircuits.com/modules/p/bindubba) | Lattice |
| D2 | [Bindubba build & BOM](https://www.nonlinearcircuits.com/s/bindubba_sequencer_build_and_bom.pdf), with schematic "bindubba3 polydirectional sequencer" and panel | Lattice |
| F1 | [32:1 product page](https://www.nonlinearcircuits.com/modules/p/32to1) | Fan |
| F3 | [32:1 build & BOM](https://www.nonlinearcircuits.com/s/32-build-BOM.pdf), with schematic "32 way bi-di router" rev 2 | Fan |
| C1 | [CD4029B datasheet](https://led.det.polito.it/datasheet/cd4029.pdf) | Lattice, Fan |
| C2 | [DG508A datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/dg508a-dg509a.pdf) (switching times of the same family) | Fan |

The owner supplied the four build documents. Still wanted: the Pseudo build
document; Electronotes 76 (the ENS-76 "Random Source" article Pseudo is based
on); the full Burhans paper.

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
- **Inputs.** Clock, reset and gate inputs use the Harriot thresholds
  (1.0 V / 0.1 V) rather than the hardware's 1.2 V; the difference matters only
  for signals that hover between them. Inputs that take audio for a comparator
  (Parallel Sum's INJECT, Roulette's TRACK) default to the hardware threshold.
- **RESET.** Harriot RESET is a rising edge that loads a seed and publishes it.
  Where the hardware has a reset, it is a level that holds the state while high;
  that difference is noted per module.
- **Events closer than a strobe allows.** Where a module has more than one
  source of change (Lattice's two clocks and jumps), a change that lands on a
  sample whose CLK must be low is applied at once and its strobe published on
  the next sample, carrying the word as it then stands. No change is lost; a
  clocked reader may see two changes as one word.
- **Determinism.** Every pseudo-random sequence here is a pure function of the
  seed and the edge count. Nothing uses a free-running random generator.
- **Saving.** Rack saves the controls and the module's state; reload publishes
  the saved word and does not resume a fractional clock phase. Rack's
  Initialize restores default controls and the documented reset state.
- **Rates.** The internal RATE ceiling stays at 2048 Hz, the ByteBusLab
  convention. Tone use (Wheel, Parallel Sum, Fan) runs from CLOCK fed by an
  oscillator, up to half the engine rate, or from Engine rate. Pseudo itself
  needs an external clock to run (P1).

## 4. Wheel (from Pseudo)

### 4.1 What the sources establish

All from P1 unless noted; the Pseudo build document has not been read.

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
feedback is XOR or XNOR. The Pseudo build document would settle all of these.
None is needed for the Harriot design above; each would change only a default.

## 5. Parallel Sum (from Burhans Timbre)

### 5.1 What the sources establish

- **Documented (B1, B4).** Three uses: timbre generator, CV pattern sequencer,
  controllable pseudo-random noise. The CV output is the sum of the voltages
  set by the pots of the active stages; several stages are usually active at
  once. A signal input changes and randomises patterns; a gate output fed back
  into it makes shorter loops. As a timbre generator the clock normally runs
  much faster than the input signal; signal and clock can be swapped.
- **Documented (B2, B4).** Burhans' Fig. 1: six shift-register stages; each
  stage has a sequence selector into a chain of XOR gates whose last input is
  an external input and whose output feeds stage 1; each stage has an output
  selector through a resistor into a summing amplifier. Fitch's changes: pots
  instead of output selectors, sequence selectors that choose **Q, off or
  not-Q**, a reset input, and a gate output per stage.
- **Documented (B4 schematic).**
  - Six stages: two CD40175 quad D flip-flops, all four of one and two of the
    other (the other's spare inputs grounded), clocked together on the rising
    edge.
  - Six on-off-on switches, panel legend Q up, OFF centre, not-Q down; the
    common goes into the XOR chain with a 100 k pulldown, so off is 0. Two
    CD4070s make the chain; the signal comparator is its last input; its output
    is stage 1's D input.
  - CLK and SIGNAL: hardware threshold with 2M2 positive feedback, about
    0.1 V of hysteresis at the jack. RST: an inverting comparator into the
    40175s' active-low CLEAR, so a high reset holds every stage at 0.
  - Output: each stage drives a 100 k pot to ground; each wiper goes through
    100 k into an inverting summer (51 k feedback), then an inverter
    (100 k / 100 k) and 1 k to OUT. The sum is positive.
  - Six gate outputs, one per stage: an emitter follower into a 2k2 / 2k2
    divider, about 5.7 V high, with an LED. Ten jacks: six gates, CLK, SIGNAL,
    RST, OUT.
- **Inferred.** Stage *k* of a shift register holds the bit that entered *k*
  clocks earlier, so a weighted sum of stages is a finite impulse response
  filter on the serial stream: `y(n) = Σ w_k · x(n − k)`. That is what makes
  the parallel sum a timbre control: the weights are filter coefficients at the
  clock rate.
- **Inferred.** A not-Q tap is a Q tap XORed with 1. The six switches
  therefore reduce to which stages are tapped plus the parity of how many are
  on not-Q: an odd number turns the chain from XOR into XNOR. With XOR, the
  all-zero state that RST leaves is a lockup, and the register stays silent
  until SIGNAL supplies a 1; with XNOR, the lockup is all ones and a reset
  register runs at once.
- **Inferred.** With logic high at about 12 V, one stage at full pot adds about
  0.51 × 12 ≈ 6 V before wiper loading, so two or three active stages reach the
  output stage's rail and clip.

### 5.2 Design

**Register.** Eight stages, one bus word; stage 0 newest (the hardware's stage
1). On each acquisition edge:

```
new  = parity(word AND MASK) XOR SENSE XOR inject
word = (word << 1 | new) AND FF
```

- MASK selects the feedback stages (knob 00–FF, or the MASK bus when patched,
  its raw data lines reclocked on this module's edge as Counter reclocks
  BUS IN). MASK 00 is no feedback: the register is a delay line for the
  injected bits.
- SENSE is XOR or XNOR: the hardware's parity of not-Q switches. With XNOR the
  lockup state is FF instead of 00.
- The six-stage hardware is MASK within 3F with W6 and W7 at 0: stages 6 and 7
  then only delay, and the recurrence and the first six bits are the
  hardware's. Six masks give the six-stage maximal period of 63: 21 2D 30 33 36
  39.
- Sixteen masks give the eight-stage maximal period of 255 from any word but
  the lockup: 8E 95 96 A6 AF B1 B2 B4 B8 C3 C6 D4 E1 E7 F3 FA. Over the 128
  masks that tap stage 7, the cycles reached from 01 under XOR have 27
  different lengths, from 8 to 255. (Checked: §10.)
- Defaults: MASK B8 (taps 8, 6, 5, 4 in 1-based numbering), SENSE XNOR, SEED
  00. From the hardware's reset state the register runs at once, and B8 under
  XNOR is a full 255-step cycle. No zero escape (*original*, deliberately
  unlike Wheel): an escape would turn MASK 00 into a walking bit.
- `inject` is INJECT through a comparator at THRESHOLD (−5…+5 V, default the
  hardware's +1.2 V, ±0.05 V hysteresis), sampled on the edge. Unpatched, it
  is 0.
- RESET loads SEED on its rising edge (Harriot); the hardware holds the
  register at 00 for as long as RST is high.

**Weights.** W0–W7, one per stage, −1…+1 with a centre detent (*original*: the
hardware's pots are unipolar; negative weights add the highpass and
zero-mean shapes the hardware cannot make). W CV is a polyphonic input:
channel *k* adds `V/5` to W*k*; the sum is clamped to ±1.

| Output | Value |
|---|---|
| BUS OUT | the register word, 9 channels, CLK on each edge |
| CV OUT | `1.25 V × Σ W_k · b_k`, bits as 0/1: a stepped CV, ±10 V at most |
| AUDIO OUT | `1.25 V × Σ W_k · (2b_k − 1)`, bits as ±1: zero-mean for a balanced stream |
| GATE OUT | stage 0, the newest bit, 0/10 V |
| CLK OUT | word-clock strobes |

HOT (option, off by default): 6 V per stage instead of 1.25 V, with a hard clip
at ±10.5 V on both outputs, as the hardware's summer clips.

What the weights do, at clock rate *f*: all eight +1 is a moving sum, a lowpass
with nulls at multiples of *f*/8; alternating signs is a highpass; one
nonzero weight is a single delayed bit. Both outputs hold between edges, so a
lowpass filter after them removes the stepping when the clock is audio rate,
as B4 suggests. GATE OUT patched into INJECT adds stage 0 to the feedback,
changing the effective mask: the "shorter loops" of B1.

### 5.3 Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Auto (CLOCK jack, else Bus CLK of MASK, else Internal), CLOCK jack, Bus CLK, Internal, Engine rate |
| RATE | 0.125–2048 Hz, default 8 Hz |
| MASK | Integer 00–FF, default B8; replaced by the MASK bus when patched |
| SENSE | XOR, XNOR; default XNOR |
| THRESHOLD | −5…+5 V, default +1.2 V |
| W0–W7 | −1…+1, centre detent; default all +1 |
| HOT | off/on |
| SEED, LOAD | default 00 |
| CLOCK, RESET, INJECT, MASK (bus), W CV (poly) | inputs |
| Outputs | as above |

About 16 HP. Hex displays show the word and the mask.

### 5.4 Not established

The summer's exact transfer with the pots' wiper loading, which shapes only
HOT's curve. The owner has no Burhans Timbre, so that stays as computed.

## 6. Roulette (from Wangernumb)

### 6.1 What the sources establish

- **Documented (W1, W2, W5 text).** Track makes the PLL a frequency tracker
  whose tracking the Range CV and filter pot make good or, more accurately,
  poor; 12 divisions of the tracker's output down to /4096. A Roulette input
  generates semi-random selections of outputs while it is high, or is a burst
  generator whose frequency the VCO CV sets; the outputs spin while it is high
  and halt when it returns to zero. A Divide input makes the 12 stages a
  divider of that input, the PLL then free as a separate VCO.
- **Documented (W5 schematic).**
  - CD4046. Its signal input (PCAIN) comes from a comparator on the Track
    jack at the hardware threshold, without hysteresis; unpatched, the jack's
    switch grounds it. Its comparator input (PCBIN) is the VCO output itself:
    no divider in the loop.
  - **Phase comparator 1, the XOR,** through 1 k to the loop filter; PC2 is
    unused. The Roulette jack's switch normals PC1 into the filter, so a
    cable in Roulette replaces the comparator.
  - Loop filter, lead-lag: the 100 k filter pot as a series resistance, then
    2 µF (two 1 µF in parallel) in series with 47 k to ground; VCOIN is the
    junction of pot and capacitor.
  - VCO: C1 4.7 nF; R1 the vactrol's LDR to ground; R2 open, so there is no
    minimum frequency and the VCO can stop; INH grounded.
  - Range: the range pot (through 47 k) and the Range CV jack (10 k / 100 k)
    summed at an NPN emitter follower whose emitter drives the vactrol's LED
    through 4.7 k. More voltage, more LED current, lower LDR resistance,
    higher VCO maximum.
  - Counter: two CD4024 7-stage ripple counters, the first's Q7 clocking the
    second; 7 + 5 = 12 outputs; their resets are grounded. Their clock comes
    from an inverting comparator on the Divide jack, whose switch normals the
    VCO output through 1 k: a cable in Divide replaces the VCO.
  - Outputs: each stage drives an emitter follower, an LED and a
    3k3 / 6k8 divider, about 6.3 V high (W3 measured about 7 V); VCO out
    through 2k2 / 2k2, a square of about 6 V. 17 jacks: Track, Roulette,
    Divide, Range CV, VCO out, 12 stages.
- **Inferred.** The CD4024 advances on a falling clock edge; behind the
  inverting comparator, the count advances on rising edges of the VCO or the
  Divide input.
- **Inferred: the spin and the coast.** A gate into Roulette drives the loop
  filter directly: VCOIN rises, the VCO runs up toward its maximum, and the
  counter spins. When the gate falls, VCOIN drops at once by the lead
  fraction `47k / (pot + 47k)` of the capacitor's voltage, then decays with
  `τ = (pot + 47k) × 2 µF`, 94–294 ms. At the pot's minimum the VCO follows the
  gate and stops dead; at its maximum it falls to about two-thirds speed and
  coasts to rest in about 1.2 s (to 1 % of full speed). The filter pot is the
  wheel's friction.
- **Inferred: "very badly behaved" tracking.** The XOR's square goes through
  the lead resistor to VCOIN: 98 % of each swing at the pot's minimum, 32 % at
  its maximum. The VCO is frequency-modulated by its own comparator at twice
  the input frequency, besides the slow 94–294 ms loop.
- **Inferred: silence until input (W3).** With Track unpatched, the XOR's
  output equals the VCO's own square. A VCO stopped with its output low gives
  0, nothing charges the filter, and it stays stopped: a resting state that
  Track or Roulette has to break. A stopped-high VCO would charge the filter
  and start.

### 6.2 Design

The loop is the circuit, in volts normalised to the 12 V supply (0…1):

- **Comparator.** XOR (default, as the hardware): `u = trackBit XOR vcoBit`.
  Edge (*original*): the 4046's PC2, driving `u` to 1 while the input edge
  leads, to 0 while the VCO leads, and leaving the filter floating in between.
  TRACK passes a comparator at the hardware's 1.2 V (±10 mV of hysteresis,
  *original*, against chatter on slow edges).
- **ROULETTE patched** replaces the comparator: `u = clamp(V / 12, 0, 1)`. A
  10 V gate is 0.83, as on the hardware. TRACK still feeds nothing while
  ROULETTE is patched, as on the hardware.
- **Filter.** Series resistance `Rs = FILTER + 1 kΩ` (the 1 k only on the
  comparator's path), `R2 = 47 kΩ`, `C = 2 µF × SCALE`:

  ```
  i      = (u − vC) / (Rs + R2)
  dvC/dt = i / C
  VCOIN  = vC + i · R2
  ```

  integrated exactly per sample (an exponential step) so that the result does
  not depend on the engine rate.
- **VCO.** `f = F_MAX × VCOIN`, stopping below VCOIN 0.01 with its phase and
  output held (*provisional*: the 4046 VCO is treated as linear from zero,
  pending §9.1). F_MAX is RANGE, 1 Hz–20 kHz on a log scale, default 2 kHz,
  times `2^(RANGE CV)`: 1 V per octave (*original*), through a vactrol-like lag,
  5 ms rising and 50 ms falling (*provisional*). A Vactrol law fitted to the
  owner's unit (threshold, slope, lag) is a later option (§12).
- **VCO OUT.** The VCO's square, ±5 V, band-limited (polyBLEP). The phase
  accumulator that makes it also clocks the counter and feeds the comparator,
  so they never disagree.

**The counter.** Twelve bits, binary, counting each rising edge of the
selected clock:

- SOURCE Auto: DIVIDE if patched, else the VCO. The VCO is passed to
  ClockOwner as its internal clock, with RATE replaced by the VCO frequency
  each sample; ClockOwner caps it at half the engine rate, so every count gets
  a strobe. The hardware's ripple counters settle in a fraction of a
  microsecond; here all twelve bits change on one sample.
- Bit *k* divides by 2^(k + 1): /2 on bit 0, /4096 on bit 11.
- RESET (*original*; the hardware has none) sets the count to 0, restarts the
  VCO's phase (ClockOwner restarts its internal clock) and publishes the count.
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
| RANGE | F_MAX, 1 Hz–20 kHz log, default 2 kHz |
| FILTER | series resistance 0–100 kΩ, the hardware pot's range; default 50 kΩ |
| SCALE | ×0.1, ×1 (default, the hardware's 2 µF), ×10 on the capacitor (*original*) |
| COMPARATOR | XOR (default), Edge |
| TRACK | audio input |
| ROULETTE | CV or gate, replacing the comparator while patched |
| DIVIDE | clock, 1.0 V / 0.1 V |
| RANGE CV | 1 V/octave on F_MAX, through the lag |
| RESET | trigger |
| Outputs | as above |

About 14 HP. A frequency readout shows the VCO; LEDs show the twelve bits.

### 6.4 Not established

The 4046 VCO's law against VCOIN; the vactrol's range and response, which set
F_MAX and how Range CV moves it; whether the self-modulated tracking and the
resting state behave as inferred. §9.1 measures each on the owner's unit.

## 7. Lattice (from Bindubba)

Implemented in ByteBusLab 2.13; the contract is [LATTICE.md](../vcv/bytebus/docs/LATTICE.md),
which supersedes §7.2–7.3 where they differ. §7.1 and §7.4 remain the record of
the sources.

### 7.1 What the sources establish

- **Documented (D1, D2 text).** A Eurorack version of the bindubba3 sequencer,
  16 stages in a 4 × 4 grid; two clock inputs for horizontal and vertical
  stepping; two direction inputs; two jump inputs that move the step to a
  position that depends upon the 3rd and 4th bits of the count of the other
  clock, semi-random; 16 gate outputs; two identical CV outputs and an inverted
  one; two slew outputs, initially the same as the CV, each slewed by a pot and
  a CV, more CV meaning less slew; intended for use with the Divide & Conquer.
- **Documented (D2 schematic and panel).**
  - Two CD4029s in binary mode, carry-in grounded (always counting), no
    reset. One counter is clocked from "main clk", the other from "2nd clk",
    each with its own jump and up/dn inputs, all at the hardware threshold.
  - Direction: each up/dn comparator inverts into the counter's UP/DOWN pin:
    up while the input is low, down while it is high.
  - Jump: each jump comparator drives its counter's preset enable.
  - Jam inputs. Main counter: J1 ← own Q3, J2 ← own Q4, J3 ← other's Q1,
    J4 ← other's Q2. Second counter: J1 ← own Q4, J2 ← own Q3, J3 ← other's Q1,
    J4 ← other's Q2. So a jump loads a counter's position (Q1, Q2) from its own
    upper bits, the second counter's with the two bits swapped, and its upper
    bits from the other counter's position.
  - Grid: each counter's Q1 and Q2 address a CD4052. The second counter's
    has both commons tied to +12 V, a 1-of-4 decoder that powers one row of
    step pots. The main counter's has its Y common at +12 V (a decoder) and
    its X common at the summer's input, selecting one column of wipers, each
    through 100 k. So the CV is the one pot at the selected row and column.
    The inverting summer (100 k feedback) is the inverted output; a second
    inverter gives the two identical CV outputs.
  - Gates: sixteen CD4081 AND gates combine one line from each decoder, one per
    stage. Each drives a gate output (emitter follower, 1 k / 1 k divider,
    about 5.7 V) and an LED.
  - Slew: per output, 1 k plus an LDR in series into a 220–470 nF capacitor,
    buffered, 1 k out; the vactrol's LED is a transistor current sink whose base
    takes the slew pot (through 47 k) and the slew CV (through 22 k). More CV,
    more current, lower LDR resistance, less slew.
  - Panel: CLOCK, JUMP and DIRECTION each as a horizontal and a vertical
    jack; SLEW CV 1 and 2; outputs GATES 1–16, SLEW 1, SLEW 2, SEQ OUT 1 and
    2, INV.
- **Documented (C1).** The CD4029's preset enable is asynchronous, and the
  preset must be low before a rising clock edge for that edge to count.
- **Inferred: what a jump settles to.** An asynchronous preset held high
  keeps each output equal to its jam input (the datasheet does not say so in
  as many words). The jam wiring is then a short chain: a counter's position
  first takes its own old upper bits, those upper bits take the other
  counter's position, and the position follows them. Any jump longer than a
  few gate delays (well under a microsecond) settles with **the main
  counter's position equal to the second counter's, and the second counter's
  position equal to the main counter's with its two bits swapped**. The upper
  bits decide the result only for a pulse too short to settle. Fitch's
  description, upper bits from the other clock, matches the J3 and J4 wiring;
  which result the hardware actually gives is §9.2's first measurement.
- **Inferred.** While a jump is held high, that counter's clock edges do
  nothing and its position follows the other counter's as it moves.
- **Inferred.** Typical slew time constants are about 0.5 ms with the LDR lit
  to 470 ms with it dark (1 MΩ off resistance, per D2's note, and 470 nF).

### 7.2 Design

**State.** H and V, each 4 bits, 0–15, binary. The step is
`s = 4 × (V AND 3) + (H AND 3)`, 0–15: V is the row, H the column. H is the
main counter (*assumed*, pending §9.2 item 1).

**Clocks.**

- H advances on H CLOCK, or on the internal RATE clock when H CLOCK is
  unpatched (ClockOwner, SOURCE as Counter's without Bus CLK).
- V advances on V CLOCK; unpatched, on H's position wrap, the edge where H's
  low two bits go from 3 to 0 counting up or 0 to 3 counting down
  (*original*). With nothing patched but a clock, Lattice is a plain 16-step
  sequence 0, 1, … 15.
- DIR H and DIR V: while high, that counter counts down, as on the hardware.
  A counter wraps modulo 16.

**Jumps.** With BUS IN unpatched:

| JUMP mode | JUMP H | JUMP V |
|---|---|---|
| Follow (default; the settled hardware result) | H's position becomes V's position and H's bits 2–3 become V's position: H = 5 × (V AND 3) | V's position becomes H's position with bits 0 and 1 swapped, and V's bits 2–3 become H's position |
| Load (the jam values taken once) | H's position becomes H's old bits 2–3, and H's bits 2–3 become V's position | V's position becomes V's old bits 2–3 swapped, and V's bits 2–3 become H's position |

- In Follow, a JUMP held high keeps that counter following the other's
  position and ignores its clock, as the hardware is inferred to; it resumes
  counting from there when the jump falls. In Load, a jump acts on its rising
  edge only.
- In Follow, JUMP H lands on the diagonal steps 0, 5, 10, 15; JUMP V lands on
  steps 0, 9, 6, 15 for H's positions 0, 1, 2, 3.
- Simultaneous jumps (*original*): both evaluate from the state before the
  jump. On the hardware the two presets would form a loop with no input, and
  hold whatever propagates first.
- BUS IN (*original*): when patched, JUMP H loads all of H from bits 0–3 of
  BUS IN's raw word and JUMP V all of V from bits 4–7, the same layout as
  BUS OUT, on the rising edge. BUS OUT → Lookup → BUS IN jumps to a table
  function of the state.

**Outputs.**

| Output | Value |
|---|---|
| CV OUT | step knob *s*, in the CV RANGE |
| INV OUT | −CV OUT (documented: the hardware's first summer is inverting) |
| SLEW A, SLEW B | CV OUT through a one-pole lag each: time constant from its knob (0.5 ms–500 ms, log, the hardware's range) multiplied by `2^(−CV)`, so each volt halves it; the CV path through a vactrol-like lag, 5 ms rising, 50 ms falling (*provisional*) |
| BUS OUT | `V × 16 + H`: the whole state, upper bits included; 9 channels, a strobe on every change (§3 for close events) |
| STEPS 1–8, STEPS 9–16 | one-hot gates, 0/10 V, 8 channels each, unclocked |
| CLK OUT | the state strobes |

RESET or LOAD (*original*; the hardware has no reset) sets H to SEED bits 0–3
and V to bits 4–7 and publishes.

### 7.3 Controls and ports

| Item | Behavior |
|---|---|
| STEP 1–16 | sixteen knobs, 0…1 of CV RANGE, in a 4 × 4 grid |
| CV RANGE | 0–10 V (default), 0–5 V, ±5 V |
| SOURCE | H's clock: Auto (H CLOCK jack, else Internal), H CLOCK jack, Internal, Engine rate |
| RATE | internal H clock, 0.125–2048 Hz, default 8 Hz |
| JUMP mode | Follow, Load |
| SLEW A, SLEW B | knobs |
| SEED, LOAD | state byte |
| H CLOCK, V CLOCK, DIR H, DIR V, JUMP H, JUMP V, RESET | inputs, 1.0 V / 0.1 V |
| BUS IN | jump targets |
| SLEW A CV, SLEW B CV | inputs |
| Outputs | as above |

About 26 HP. LEDs under each step knob; a hex display shows the state.

### 7.4 Not established

Whether a hardware jump gives the Follow or the Load result; which counter is
horizontal on the panel; which knob is which step (the decoder-to-pot wiring
was not traced); the CV output's top (one step pot at full drives the summer
toward its rail); the vactrol's curve. §9.2 settles all of these on the owner's
unit.

## 8. Fan (from 32:1)

Implemented in ByteBusLab 2.12; the contract is [FAN.md](../vcv/bytebus/docs/FAN.md),
which supersedes §8.2–8.3 where they differ. §8.1 and §8.4 remain the record of
the sources.

### 8.1 What the sources establish

- **Documented (F1, F3 text).** Splits one input into 32 outputs or switches
  32 inputs into one; reset gives lower counts; a direction control; with
  nothing in the in/out jack it is a 32-stage counter; five LEDs show the count
  in binary. Inspired by the 32-stage counter/multiplexer of Julian Driscoll
  and Warren Burt, simplified around 16:1 analogue switches that pass any
  voltage within the rails. Out 1 gives an extra trigger on count 17, from a
  slight delay in switching the multiplexers' enable (F1).
- **Documented (F3 schematic).**
  - Two ADG406 (DG406 or DG506 in the BOM) 16:1 multiplexers, their commons
    joined, 470 Ω in series with every channel and the common.
  - Two CD4029s in binary mode, cascaded synchronously (shared clock, the
    first's carry-out into the second's carry-in). The first's Q1–Q4 address
    both multiplexers; the second's Q1 is bit 4.
  - Bit 4 enables the second multiplexer directly, and the first through an
    inverting TL074 comparator.
  - Clock, reset and up/down at the hardware threshold. Reset drives both
    counters' preset enable with every jam input grounded: count 0, held while
    reset is high. Up/down inverts into UP/DOWN: up while the input is low.
  - The in/out jack is normalled to a 10 k / 10 k divider from +12 V, 6 V
    through 5 kΩ: counter mode.
  - 36 jacks: 32 outputs, in/out, clock, reset, up/down.
- **Inferred: the glitch.** When bit 4 rises, the second multiplexer turns on
  at once, while the op-amp inverter turns the first off only after its slew
  and saturation recovery, about a microsecond or more (TL07x behaviour; C2's
  switching times are of the same order). For that moment the first is still
  on at the new low address. Counting up, 16 → 17 leaves out 1 connected with
  out 17: the documented count-17 trigger. Counting down, the wrap 1 → 32 does
  the same at address 15, out 16 with out 32 (*predicted*). When bit 4 falls
  (17 → 16 down, 32 → 1 up), the late enable leaves a moment with neither on:
  a dropout, not a glitch (*predicted*).
- **Inferred.** "Reset gives lower counts" is the usual CMOS trick: an output
  patched into reset presets the count to 0 when it is reached, so the count
  runs 0 … N − 1 with step N appearing for an instant.

### 8.2 Design

**Address.** 5 bits, 0–31, shown 1–32 on the panel.

- With BUS IN unpatched, an internal counter (ClockOwner) counts on each
  edge: up, or down while DIR is high or the DIR switch is Down (the gate
  inverts the switch; the hardware has the gate only); wrapping at LENGTH,
  1–32, so it visits 0 … LENGTH − 1 (*original*, the knob doing what the
  patched-reset trick does). EOC pulses on each wrap. RESET loads SEED, 0–31,
  on its rising edge; the hardware holds 0 while reset is high.
- With BUS IN patched, the address is bits 0–4 of BUS IN's word at its sample
  instants, and Fan is transparent: it forwards BUS IN's clock and mode. LENGTH,
  DIR and the internal clock are unused (*original*: Counter or Lookup supply
  any order).

**Routing.** Rack ports are one-directional, so the hardware's single in/out
jack becomes COM IN and COM OUT, and the 32 jacks become two 16-channel
polyphonic cables per direction, one per multiplexer. Both directions run at
once from one address.

| Port | Role |
|---|---|
| COM IN | demux source, mono; unpatched, normalled to 10 V, which makes OUT 1–16 and OUT 17–32 one-hot gates (the hardware's counter mode, at Harriot's gate level rather than the hardware's 6 V through 5 kΩ) |
| OUT 1–16, OUT 17–32 | 16 channels each; the selected channel carries COM IN |
| IN 1–16, IN 17–32 | 16-channel polyphonic inputs; missing channels read 0 V |
| COM OUT | the selected input channel |

UNSELECTED (menu): **Zero** (default; an off ADG406 channel is high
impedance, which a 100 k input reads as 0 V) or **Hold** (each output keeps the
last value it carried while selected).

**GLITCH** (option, off by default) models the late first-multiplexer enable,
for one engine sample, the shortest Rack event:

- On an address change that raises bit 4, both halves are selected at the new
  low address: in demux, COM IN reaches both OUT channels; in mux, COM OUT is
  the mean of the two inputs, as the two 470 Ω paths would join them. Up
  16 → 17 gives out 1 with out 17 (documented); down 1 → 32 gives out 16 with
  out 32 (predicted).
- On an address change that lowers bit 4, neither half is selected for that
  sample: outputs follow UNSELECTED, COM OUT is 0 V (predicted).

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

The enable skew's actual length and the predicted down-count and dropout
cases. The owner has no 32:1; nothing here depends on them except GLITCH's
undocumented cases.

## 9. Measurement plan (the owner's Wangernumb and Bindubba)

Method: drive test clocks and gates from Rack through the ES-3, and record the
module's outputs through the ES-6 into Rack, so stimulus and response share a
clock and every edge is timed to the sample. A scope is fine for anything
marked *by eye*. Results go in `vcv/bytebus/tests/hardware/` as CSV with the
patch that produced them.

### 9.1 Wangernumb

1. **Resting state.** After power-up with nothing patched: is VCO out stopped?
   Patch Track with audio for a second and remove it: does the VCO keep running
   (the XOR's own square holding the filter mid-way) or fall back to rest?
2. **Coast.** Steady 10 V into Roulette for 1 s, then 0 V; record VCO out for
   3 s after, at the filter pot's minimum, middle and maximum. Predicted: a
   dead stop at the minimum; at the maximum an immediate drop to about
   two-thirds speed, then a 0.29 s decay, stopped after about 1.2 s.
3. **VCO law.** Roulette at 0, 2, 4, 6, 8 and 10 V (steady), range pot fixed:
   VCO frequency at each. Linear through zero is the draft's assumption.
4. **Range law.** Roulette at a steady 10 V; VCO frequency at the range pot's
   ends and three points between, then Range CV 0–10 V in 1 V steps with the
   pot at its minimum. This fits the vactrol law and the RANGE scale.
5. **Vactrol time.** Roulette at 10 V; step Range CV 0 → 5 → 0 V; record VCO
   frequency over time, rise and fall.
6. **Tracking.** Track at 100 Hz, then stepping 100 → 200 Hz, at the filter
   pot's minimum, middle and maximum: VCO out's period over time (lock, wobble,
   slip).
7. **Edge and levels.** That /2 changes on VCO out's rising edge; gate high
   level (predicted about 6.3 V; W3 measured about 7 V) and VCO out's level
   (predicted about 6 V).

### 9.2 Bindubba

1. **Grid.** Clock the horizontal clock only, then the vertical only: which
   LEDs light in which order; which panel clock is the schematic's "main".
2. **Jump result.** Clock both axes to a known pair of positions (watch the
   LEDs), pulse the horizontal JUMP and note where the step lands; repeat for
   all sixteen positions, then for the vertical JUMP. Follow predicts the
   diagonal for the main counter's jump and the swapped position for the
   second's (§7.2). Load predicts landing on the counter's old upper bits,
   which depend on how many clocks it has seen; it would not repeat from the
   same visible position.
3. **Held jump.** Hold the horizontal JUMP high and clock the vertical: does
   the column follow the row as Follow predicts?
4. **Direction.** Confirm that a high level counts down.
5. **CV.** Each output with the step pot at its ends: the top level (where the
   summer clips), and INV as −CV.
6. **Slew.** A full-scale step through each slew output at three pot settings,
   and at 0, 2.5 and 5 V slew CV: time constants rising and falling.
7. **Gates.** High level of the sixteen gates (predicted about 5.7 V).
8. **Knob map.** Which knob each of the 16 positions plays.

## 10. Testing

Arithmetic stated in this draft was run before writing it:

- the R1 taps give 2^24 − 1 from any nonzero seed; with the escape the cycle
  is 2^24, every state visited once; none of the 23 degree-24 trinomials is
  maximal;
- the sixteen maximal 8-bit masks, the six maximal 6-bit masks, the 27 cycle
  lengths; B8 under XNOR runs a 255-step cycle from 00 and locks only at FF;
- the binomial counts; Lattice's unpatched-V raster; the settled jump results
  of the jam wiring; the Fan overlap and dropout transitions;
- the hardware threshold (1.200 V), the Burhans hysteresis (0.095 V), the
  Roulette filter's time constants and lead fractions, and the gate levels.

Each module then gets, as the existing ByteBusLab modules do:

- **Portable engine tests** (`tests/engine_test.cpp`).
  - Wheel: the full 2^24 cycle and state coverage; window reads for every
    OFFSET and STRIDE; each LENGTH's loop period; capture landing on the next
    edge; reset determinism; G's distribution over the cycle.
  - Parallel Sum: all 256 masks under XOR and XNOR against a reference
    stepper; the six-stage masks against a six-stage reference; CV and AUDIO
    equal to direct convolution of the serial stream; HOT clipping; inject;
    W CV clamping.
  - Roulette: count bits as exact divisions; the filter against a
    high-resolution integration of the same equations at 44.1/48/96 kHz;
    coast times against the closed form; the resting state; one RESULT strobe
    per stop; VCO capped at half the engine rate with every count strobed;
    ROULETTE replacing the comparator.
  - Lattice: all 256 states under every input combination against a reference
    transition function; Follow and Load jumps against the jam wiring settled
    and taken once; held jumps; simultaneous jumps; BUS IN jumps; the raster;
    strobe deferral under random clocks and jumps.
  - Fan: every address, LENGTH and direction; mux and demux routing; Hold;
    GLITCH overlap and dropout exactly on bit-4 changes; bus addressing.
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
  Parallel Sum, Wheel, Roulette (§9.1 first; the loop needs the most fitting).

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
4. **Parallel Sum.** Eight stages with MASK and SENSE (proposed) or six
   three-way switches as on the hardware; bipolar weights (proposed) or
   unipolar; defaults XNOR and SEED 00 (proposed, so a reset register runs) or
   XOR and 01; whether RESET should also offer the hardware's hold-while-high.
5. **Roulette.** The circuit loop (proposed); FILTER at the hardware's range
   with SCALE (proposed); Range CV at 1 V/octave (proposed) or the fitted
   vactrol law after §9.1, or both as a menu; the RESULT bus.
6. **Lattice.** Follow as the default jump (proposed, pending §9.2 item 2) or
   Load; H as the main counter (pending item 1); V normalled to H's position
   wrap; BUS IN jumps.
7. **Fan.** Two 16-channel cables per direction (proposed) or 32 mono jacks;
   GLITCH with the predicted cases (proposed) or the documented one only.
8. **Build order** (§11).
9. **Contacting Nonlinearcircuits** before these are published, as a
   courtesy; the names already avoid theirs.
