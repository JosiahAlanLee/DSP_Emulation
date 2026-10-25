# Vastish on the byte bus

Spec **v0.1, draft** (2026-10-25). Depends on
[`docs/BYTE_BUS_SPEC.md`](../../docs/BYTE_BUS_SPEC.md). Target: the next
Vastish 2.x. This is the design record; the manual gets the user-facing
version.

## 1. Purpose

Vastish takes three roles on the bus, with what it already has:

1. **Converter.** Any Vastish signal or source goes out as a word; a word
   comes in as a signal or a source.
2. **Byte-wide ALU.** Its eight voices become eight bit lanes. The four FUNs,
   with their existing equations, become per-bit AND, OR, XOR, AND-NOT and
   NOT, four operations deep, with the whole modulation matrix available per
   bit.
3. **Transfunctioner.** A word in, the F1–F3 chain as the transform, a word
   out. This is the Jena-class use, with the matrix standing in for the
   hardware's shape and phase CV inputs.

Nothing that exists changes unless a new option is chosen. Everything new is
appended, so saved patches keep working.

## 2. What already works, with no code

- **Bus → AUX *n*.** Voice *v* gets bit *v* as 0 or 1 (10 V × 0.1 = 1).
  Unclocked. One difference from the protocol: AUX broadcasts a 1-channel
  cable to every voice (`getPolyVoltage`), while the bus reads a 1-channel
  cable as the word 0 or 1.
- **Bus → GATE.** The bits gate the voices: bit-gated chords.
- **FUN *n* OUT.** Per-voice value × 10 V. When a FUN computes logic, the jack
  is already an unclocked bus.

Four limits remain, and §4–§8 remove them: FUN jacks only have as many
channels as pitch/gate polyphony; sleeping voices' FUNs are not evaluated; no
clocked reading; no reading of the word as a number.

## 3. Additions at a glance

| Kind | Addition | Ids |
|---|---|---|
| Inputs | BUS A, BUS B (poly, 8 or 9 channels) | `BUS_A_IN = 8`, `BUS_B_IN = 9`; `INPUTS` 8 → 10 |
| Output | BUS OUT (poly, 0, 8 or 9 channels) | `BUS_OUT = 11`; `OUTPUTS` 11 → 12 |
| Sources (matrix and FUN A/B) | BUS A, BUS B, LANE A, LANE B, ONE | 18–22; `SOURCE_COUNT` 18 → 23 |
| Pitch wave | BUS | 6 (`BUS_WAVE`); `pitch-wave` hi 5 → 6 |
| Params | BUS OUT mode (OFF / WORD / LANES), BUS OUT source | 80, 81; `PARAM_COUNT` 80 → 82 |
| Engine | the control section runs for all eight voices while a bus is connected | — |

Panel: the bottom row has room to the right of the last output (about
x 690–790 at y 348), so the three ports fit without widening the 56 HP panel.
The two params are context-menu settings in v0.1; they are still params, so
they save with the patch and can be mapped.

## 4. Bus inputs

- Each input has a `dspemu::bus::Reader`, processed once per sample before
  the engine tick.
- Clocked (≥ 9 channels): the word updates on a CLK rising edge and holds
  between edges. Unclocked: it updates every sample.
- **Unconnected input:** word, lane and wave readings are 0 (silent), not the
  −1 that word 0x00 would give. Connected and all-low is word 0 → bipolar −1,
  as the protocol says. (A BUS OUT in OFF mode has 0 channels, which Rack
  treats as disconnected, so OFF upstream reads as silence downstream.)
- New sources:
  - **BUS A / BUS B**: the word as bipolar, (*n* − 128)/128, the same value in
    every voice.
  - **LANE A / LANE B**: bit *v* of the word in voice *v* (*v* = 0..7), as 0
    or 1.
  - **ONE**: the constant 1. Needed for NOT (§6); VEL cannot serve as a
    constant because it is 0 until a gate has risen, and logic patches have no
    gates.
- Unipolar readings come from a FUN: (BUS A + ONE)/2 is exactly *n*/256, the
  protocol's *phase*.

## 5. Lane polyphony and the control/audio split

Rule: **while BUS A or BUS B is connected, Vastish evaluates the control
section of all eight voices every sample** (gate edges, envelopes, LFOs,
sources, FUNs), and the modulation outputs (AMP, ENV2, ENV3, LFO1, LFO2,
FUN1–4) carry eight channels. Audio polyphony is unchanged: a voice sounds only
with pitch/gate channels, as now.

Engine change: `Voice::tick` splits into `tickControl` (everything up to and
including the FUN evaluation) and `tickAudio` (oscillator, chain, amp, pan).
`Engine::tick` gains a `controlAll` flag; a sleeping voice runs `tickControl`
only when the flag is set and never runs `tickAudio`. The audio of the voices
in use must stay bit-identical with the flag in either state, which extends
the existing sleeping-voice identity test. Cost: a few hundred operations per
voice per sample; negligible next to one filter.

## 6. FUN as a byte-wide ALU

With A and B each 0 or 1, from LANE A, LANE B or ONE:

| Equation | On bits | (0,0) (0,1) (1,0) (1,1) |
|---|---|---|
| MIN, A\*B | AND | 0 0 0 1 |
| MAX, A+B, (A+B)/2 | OR (A+B gives 2 for both; (A+B)/2 gives ½ for one) | 0 1 1 1 |
| ABS(A−B) | XOR | 0 1 1 0 |
| A−B | A AND NOT B (−1 for (0,1)) | 0 −1 1 0 |
| A\*(B+1)/2 | A (pass; ½ when B = 0) | 0 0 ½ 1 |
| ONE−B, ABS(B−ONE) | NOT B | 1 0 1 0 |

The threshold that turns a value back into a bit (§8, LANES mode) is **value
\> 0**. It reads every row above correctly, turns any bipolar source into a
zero-crossing comparator (LFO1 → a square), and turns an envelope into an
"active" flag. Values on the FUN jacks are unchanged: A−B puts −10 V on the
jack for (0,1), and a downstream Schmitt reads that as 0 anyway.

Depth: FUN1 → FUN2 → FUN3 → FUN4 chaining (a cyclic edge still reads 0) gives
four-deep expressions per bit, and lanes also drive the matrix: bit *v* can
open voice *v*'s filter or move its pan.

Two buses: LANE A, LANE B, ABS(A−B) on FUN1 is the per-bit XOR of two words,
the basic Lipsk/Gera-class operation. FUN1 OUT carries it as an unclocked bus;
BUS OUT in LANES mode carries it with BUS A's clock.

## 7. BUS wave: the transfunction path

PITCH wave 6, **BUS**: the voice's source signal is *x* = bipolar(word of
BUS A) instead of an oscillator, in every voice. The chain and the algorithm
transform it; the DSP tap (§8) is the result as a word.

**Phase rotation** (the hardware's phase CV with through-zero modulation): in
BUS mode the PITCH column does not compute a frequency, which would mean
nothing here. It rotates the input instead:

```
x' = wrap[−1, 1)( x + 2 · (c − 60) / 12 )      c = the PITCH column's control value
```

So the default ADJUST 60 is no rotation; one octave of pitch control (12
semitones, 1 V at KEY 1) is one full turn of the 256-step cycle; an LFO at
depth 12 is through-zero phase modulation of ±1 cycle. KEY 0 stops V/OCT from
rotating. (A new PHASE parameter was considered and rejected for v0.1: the
column already has exactly the structure the phase input needs, and the panel
stays as it is.)

Which types are transfunctions:

- **Memoryless, true byte → byte maps:** NONE, SHAPER, DIST, GAIN, CRUSH.
  SHAPER at ADJUST 2 is one sine cycle per 256 steps (cos(2π(*x*/2 − ¼)) =
  sin(π*x*)); 4 is two cycles; the amount is the harmonic number, and
  fractional values fold. CRUSH at ADJUST *k* keeps roughly the top *k* bits
  (it rounds rather than truncates; *k* = 8 is the identity).
- **With memory:** LOPASS, HIPASS, LP2RES, BANDPASS, NOTCH, LP4, COMB and
  DECIMATE see the input as a staircase (clocked) or a 1/128-step signal
  (unclocked) and add their own dynamics; SINE and RINGMOD bring an oscillator
  in. The hardware lookup has none of these. They are the extension, not a
  shortfall.
- **No gate needed.** The DSP tap sits before the amp envelope, so the
  transfunction runs with nothing patched but the bus. L/R still need a gate,
  as always.
- **Identity:** ALG 1, all three types NONE, PITCH ADJUST 60, KEY 0: the BUS
  OUT word equals the BUS A word exactly, for all 256 values (a test).

Eight voices with different KEY, VEL or source settings in the PITCH column
are eight rotated or differently shaped versions of the same word, mixed to
L/R with their own envelopes: a polyphonic transfunctioner, for free.

## 8. BUS OUT

Params (menu in v0.1, saved with the patch):

- **Mode**: OFF (0 channels), WORD (default), LANES.
- **Source**: the 23 sources, plus **DSP** (voice 1's chain output before the
  amp envelope, the transfunction tap) and **MIX** ((L+R)/2 after MASTER, in
  ±1 DSP units). Default DSP.

Encoding:

- **WORD**: *n* = fromBipolar(value of the selected source in voice 1); ±1 is
  full scale and anything beyond clips. For DSP and MIX this is the protocol's
  ±5 V convention exactly (±1 DSP = ±5 V). For sources it is as if the FUN
  jack's ±10 V were read by a ±10 V converter; the manual says so.
- **LANES**: bit *v* = value of the selected source in voice *v* \> 0. MIX
  gives all bits the same value.

Clock: BUS OUT is clocked iff BUS A is clocked (or BUS B, when A is
unconnected). It forwards that CLK level on channel 8 and updates its word only
on that bus's sample instants. Otherwise it has 8 channels and updates every
sample. Encoding happens after the engine tick in the same `process()`, so an
edge in gives the transformed word out on the same sample; Vastish adds no
latency of its own.

## 9. Jena-class coverage

| Feature (from the published descriptions) | Here |
|---|---|
| 8 bits in → 8 bits out through a shape | BUS wave → chain → BUS OUT (DSP) |
| Shape CV | F ADJUST through the matrix: envelopes, LFOs, AUX, the other bus |
| Bank selection | F type (15) × algorithm (6) × three blocks |
| Phase CV, through-zero phase modulation | PITCH column rotation (§7) |
| 15 banks of 256 authored tables, drum-pattern bank | **Not covered.** A Lookup module with its own tables is on the roadmap |
| Per-step exactness of arbitrary tables | **Not covered.** These are computed functions, not tables |
| — | Polyphonic variants of one word, stateful transforms, per-voice envelopes: not in the hardware |

## 10. Patch recipes (also the acceptance patches)

1. **Two Vastishes, no new modules.** A: wave SAW, ALG 1, all NONE, BUS OUT
   WORD/DSP. B: BUS A patched, wave BUS, F1 SHAPER at ADJUST 2 → a sine; ENV2
   into F1 ADJUST at depth 6 sweeps harmonics 2–8 over the note. B's L/R need
   a gate; B's BUS OUT does not. This is the oscillator → transfunction pair.
2. **XOR of two buses.** FUN1 = ABS(LANE A − LANE B). Take FUN1 OUT, or BUS
   OUT in LANES/FUN1 for the clocked version.
3. **Bit-gated chords.** A counter's bus into GATE; the bits play the voices.
4. **Who is sounding.** BUS OUT LANES/ENV2: a byte whose set bits are the
   voices with an active envelope.

## 11. Compatibility

- Source ids 18–22, wave id 6, params 80–81 and the three ports are appended.
  Existing patches load unchanged: Rack fills missing params with defaults and
  absent cables stay absent.
- Program loading applies ids 0–79 only. Programs describe a sound; bus
  routing is patch wiring, and loading INIT should not silence a bus.
- `Programs.hpp` is generated by `tools/import_programs.py`; extend the
  generator for the new option names, the wave, and the two params with their
  defaults (WORD, DSP), rather than editing the output.
- `plugin.json`: minor version bump. `plugin_test` counts change from
  80/8/11 to 82/10/12.

## 12. Implementation notes

- Everything is on the audio thread: two Readers, one Writer, no allocation,
  no UI-thread state. The sample handoff and the OTIS link are untouched.
- `Voice` gains `float tap` (chain output before the amp envelope), set in
  `tickAudio`.
- Order inside `process()`: read BUS A and B → fill `VoiceInput` (word A/B,
  lane A/B per voice) → `engine.tick(…, controlAll = busConnected)` →
  modulation outputs at `busConnected ? 8 : max(1, channels)` channels →
  encode BUS OUT.
- The engine stays free of Rack types so `engine_test` keeps driving it
  directly; `VoiceInput` carries the decoded values.

## 13. Tests

Engine (`tests/engine_test.cpp`, portable):

- The truth table of §6 for every equation across all eight lanes, with LANE
  A, LANE B and ONE.
- BUS wave identity: all 256 words through ALG 1 / NONE → tap → fromBipolar
  gives the input back.
- Rotation: PITCH ADJUST 72 (one turn) is the identity; 66 (half a turn) maps
  *n* to (*n* + 128) mod 256, exactly.
- SHAPER 2 on a 256-step saw word: total harmonic power at least 40 dB below
  the fundamental.
- The sleeping-voice identity test passes with `controlAll` on and off.

Module (`plugin_test.cpp`, `adapter_test.cpp`, Rack API):

- Port and param counts; the new params serialize and restore.
- A clocked bus changes the word only on the CLK-rise sample; a frozen clock
  freezes it; an unconnected bus reads silent; a 1-channel gate reads word 0
  or 1.
- Lane polyphony: with a bus connected and no gates, the modulation outputs
  have eight channels and FUN lanes follow the bits.
- BUS OUT: channel count 0 / 8 / 9 by mode and clock; levels exactly 0 V or
  10 V; CLK forwarded on the same sample.
- Two-module acceptance, in the style of `link_test`: two Vastish instances
  wired A → B through Rack ports; recipe 1 produces a sine within the THD
  bound.

## 14. Manual updates

Ports table (+3), sources table (+5), waves (+BUS), a section "The bus" with
recipes 1–4, and the WORD-mode scaling note. `docs/DEVELOPMENT.md` gets a
"Byte bus" section beside "Sample sharing".

## 15. Decisions to ratify

1. The lane rule: bus connected ⇒ eight-channel modulation outputs (the
   alternative is an explicit menu switch).
2. Phase rotation through the PITCH column rather than a new parameter.
3. LANES threshold at value \> 0.
4. WORD mode scaling: ±1 source = full scale.
5. Programs skip the bus params.
6. Menu-only params in v0.1.
