# Byte bus: 8-bit data over Rack polyphonic cables

Spec **v0.1**, drafted 2026-10-25 and ratified 2026-10-26 (§11). Design record for `vcv/shared/ByteBus.hpp` and
for every module that reads or writes the bus. The Vastish side is in
[`vcv/vastish/BUS_SPEC.md`](../vcv/vastish/BUS_SPEC.md).

Name: **Harriot** — Thomas Harriot worked out binary arithmetic in his
notebooks around 1600, unpublished, before Leibniz. Panel label on every
module: **BUS**. Code: `namespace dspemu::bus`.

## 1. Purpose

A convention for carrying one 8-bit word, and optionally its clock, on a single
VCV Rack polyphonic cable, so that independent modules can treat signals as
binary numbers: convert audio and CV to bytes and back, operate on bits, count,
delay, look up, switch, and reconvert.

The *role set* (what kinds of module exist and what each does) is modelled on
the Leibniz Binary Subsystem by Xaoc Devices (Drezno II, Lipsk, Gera, Jena,
Erfurt, Rostock, Poczdam, Berlin), known only from its published manuals,
retailer descriptions and demonstration videos. Nothing here is wire-compatible
with that hardware, and nothing is meant to be: the hardware uses a 10-pin
ribbon at clock rates up to the megahertz range; this uses a Rack cable at the
engine rate.

## 2. Clean-room rule

- The reference for what each role *does* is the hardware's public behaviour.
  No firmware, schematics, PCB photographs or parts are consulted. None of the
  manufacturer's names, panel art or table contents (Jena's banks) are reused.
- This document and the per-module specs are the only inputs to the code. When
  a behavioural question comes up during implementation, it is settled here
  first and then implemented, never answered from the hardware documentation
  inside the code.
- Module names are ours. City names are the manufacturer's idiom; avoid them.

## 3. Carrier and channel map

One polyphonic cable.

| Channel (0-based; Rack's UI shows +1) | Meaning |
|---|---|
| 0–7 | Data bit *k* with weight 2^*k*. Channel 0 = bit 0 = LSB; channel 7 = bit 7 = MSB |
| 8 | CLK |
| 9–15 | Reserved. Receivers ignore them; senders set the channel count to exactly 8 or 9 |

The channel count carries the mode:

- **≤ 8 channels: unclocked.** Missing high channels read as 0, so a 1-channel
  gate is the word 0 or 1 and a 4-channel cable is a nibble 0–15. (Rack zeroes
  the voltages above an output's channel count, so this costs nothing.)
- **≥ 9 channels: clocked.** Channel 8 is the clock. Channels above 8 are
  ignored.

LSB-first is chosen so that short cables are small numbers, "lane *n*" means
"bit *n*", and arithmetic needs no reversal.

## 4. Levels

- Senders: a 1 bit and a high CLK are **10 V**; a 0 bit and a low CLK are
  **0 V**. Never bipolar.
- Receivers: every line, data and CLK alike, passes a Schmitt trigger: high at
  **1.0 V** or above, low at **0.1 V** or below. Between thresholds it retains
  its previous state. Non-finite voltages clear that line to low. The portable
  `Gate` helper implements these thresholds without a Rack dependency.
- Consequence: any Rack gate, trigger, clock or comparator output is a valid
  bit, and a bus can drive gate inputs and polyphonic VCAs directly.

## 5. Clock and sample instants

A **sample instant** is a moment at which a receiver takes the data lines as a
word.

- Unclocked: every engine sample is a sample instant.
- Clocked: the rising edge of CLK, sampling the data lines on that same engine
  sample. Between edges the word holds. A clocked bus whose CLK never rises
  carries a frozen word; that is correct behaviour, not an error (it is how a
  loop gets interrupted on purpose).

Two kinds of processor:

- **Transparent** (combinational): the output word is a function of the current
  input word(s). It forwards the clock of its primary input unchanged and keeps
  that input's clocked/unclocked mode, emitting exactly 9/8 channels even if its
  input is a short cable or has reserved channels. Examples: bit
  logic, masks, lookup.
- **Latching** (sequential): registers, counters, delay lines, held
  conversions. They advance on sample instants. In unclocked mode that means
  every engine sample, which makes the result depend on the engine rate, so a
  latching module **owns an internal clock generator** (set in Hz) and uses it
  whenever no CLK is present; "engine rate" is offered only as an explicit menu
  choice. Converters are the exception: an ADC or DAC with no clock converts
  every sample, since that is what makes the conversion transparent.

Timing facts:

- Rack delivers an output's voltage to the connected input on the next engine
  sample, so each module hop costs one sample (20.8 µs at 48 kHz). Data and
  CLK travel on the same cable, so there is no skew between them.
- A sender holds CLK high for at least one engine sample and low for at least
  one, so the fastest clock is half the engine rate.
- A module that emits data and CLK writes both in the same `process()` call,
  for the same sample.

Input decoding and state advancement are separate. `Reader::process()` reports
both the current decoded `rawWord` and the bus-latched `word`, along with the
clock level and edge. A module reclocking on a local clock samples `rawWord` on
its own selected edge; it must not add an unintended upstream-clock latch.
An unclocked reader updating every sample does not oblige a register to advance
at the engine rate. Sequential modules using an internal clock emit that clock
on channel 8. A connected but stopped clock never triggers a timeout fallback.

Disconnecting resets the reader. Connecting, or switching between unclocked and
clocked mode, clears its line states; an initially high CLK counts as the first
rising edge. Short-cable width changes inside the same mode do not reset it;
missing high lines are cleared. Decode all inputs before evaluating an operation.

A register's RESET may publish its seed as a new word-clock event. The
[Register contract](../vcv/bytebus/docs/MANUAL.md#clock-and-reset-timing) specifies
the low gap, reset priority and how nearby acquisition edges are suppressed.

## 6. Numeric interpretations

The wire carries *n* ∈ 0..255. Modules agree on these readings of it:

| Name | Formula | Range | Typical use |
|---|---|---|---|
| word | *n* | 0..255 | logic, counting |
| level | *n*/255 | 0..1 | unipolar CV, gains |
| phase | *n*/256 | 0..1, never 1 | table index, oscillator phase |
| bipolar | (*n* − 128)/128 | −1..+0.9921875 | audio, bipolar CV; 0x80 is exactly 0 |
| volts | 5 × bipolar | −5 V..+4.96 V | DAC output |

The cable does not tag a word's meaning. CZish/DAC Stage's encoded high bytes
need explicit conversion before these numeric readings represent the original
signal. [Adapter](../vcv/bytebus/docs/ADAPTER.md) provides CZ/Yamaha/signed
codecs, bit routing, trigger extraction and local reclocking. A frame address,
hit mask, ratio word or encoded device word is not automatically bipolar audio.

Conversions:

- DAC: V = (*n* − 128) × 5/128. One LSB is 39.0625 mV.
- ADC: *n* = clamp(floor(128 + 128·V/5 + 0.5), 0, 255). Rounds to nearest,
  half up; saturates at both ends.
- bipolar → word: *n* = clamp(floor(128 + 128·*s* + 0.5), 0, 255).
  word → bipolar → word is the identity for all 256 values (every step is an
  exact binary fraction, so float carries it without error).

Offset binary rather than two's complement, so that bit 7 reads as polarity
(0 = negative half) and an all-zero bus is full negative rather than zero.
±5 V full scale follows Rack's audio convention; converter modules add gain and
offset controls and may offer a ±10 V range.

Test vectors:

| *n* | hex | high channels | bipolar | volts |
|---|---|---|---|---|
| 0 | 00 | — | −1.0 | −5.000 |
| 64 | 40 | 6 | −0.5 | −2.500 |
| 128 | 80 | 7 | 0.0 | 0.000 |
| 165 | A5 | 0, 2, 5, 7 | +0.2890625 | +1.4453 |
| 255 | FF | 0–7 | +0.9921875 | +4.9609 |

## 7. Working with ordinary Rack modules

- Core **Merge** builds a bus from eight gates, with a ninth input for CLK;
  **Split** breaks one out. Set Merge's channel count to 8 or 9 by hand if its
  automatic count comes out otherwise.
- A bus into a monophonic input delivers bit 0 only.
- A bus into a polyphonic VCA is eight switches; into a polyphonic quantizer,
  slew or S&H it is eight independent gates. All of that is free.
- Vastish's AUX inputs already read a bus as one bit per voice (voice *n* ↔ bit
  *n*), and its FUN outputs, when they compute logic, are already unclocked
  buses (0 V or 10 V per channel). The Vastish spec removes the remaining
  limits.

## 8. Shared header: `vcv/shared/ByteBus.hpp`

OTIS's optional frame selector is specified separately in
[`vcv/otislab/docs/BYTE_BUS.md`](../vcv/otislab/docs/BYTE_BUS.md). Its bus controls
frame selection while the native stereo sample engine owns playback timing;
selected stored frames can also be exported as original Lookup tables.

Header-only, like `SampleShare.hpp`; audio-thread safe; no allocation; the
pure functions have no Rack dependency so the portable tests can cover them.

```cpp
namespace dspemu::bus {
constexpr int   BITS = 8, CLK = 8;                 // channel indices
constexpr float HIGH = 10.f, V_HIGH = 1.f, V_LOW = .1f;
constexpr float FULL_SCALE_VOLTS = 5.f;

inline float   bipolar(uint8_t n)   { return (int(n) - 128) * (1.f / 128); }
inline float   phase(uint8_t n)     { return n * (1.f / 256); }
inline float   level(uint8_t n)     { return n * (1.f / 255); }
inline float   toVolts(uint8_t n)   { return bipolar(n) * FULL_SCALE_VOLTS; }
inline uint8_t fromBipolar(float s);                // clamp(floor(128+128s+.5),0,255); non-finite -> 128
inline uint8_t fromVolts(float v)   { return fromBipolar(v / FULL_SCALE_VOLTS); }

struct Sample {
    uint8_t rawWord = 0;            // current Schmitt-decoded lines
    uint8_t word = 0;               // held when clocked; current when unclocked
    bool connected = false, clocked = false, clk = false, rising = false;
    bool updated = false;          // input word sampled, not a register tick
};
struct Reader {                     // one per bus input; retains Schmitt state
    template<class Input> Sample process(Input& in);
    void reset();
};

struct Writer {                     // one per bus output
    // 8 channels unclocked, 9 clocked; 0 V / 10 V per bit; CLK level on channel 8.
    template<class Output> void write(Output& out, uint8_t word, bool clocked, bool clk) const;
};
}
```

The helpers are implemented without including Rack. The input/output template
arguments accept Rack ports or portable test ports; there is no allocation.
Rack itself leaves disconnected outputs at zero channels. In Rack SDK 2.6.4,
calling `setChannels(0)` on a connected output actually gives a silent **one**
channel, not a disconnected port; a future OFF mode must account for this.

Tests (`vcv/bytebus/tests/engine_test.cpp`, portable, and `module_test.cpp`,
using the real Rack APIs and Vastish):

- All 256 words round-trip through bipolar, phase, level and volts and back,
  bit-exact.
- `fromVolts` is monotonic and saturates; the test vectors above hold.
- Hysteresis: a line at 0.5 V keeps its previous state; NaN reads low.
- Clocked input latches on the edge sample only and holds between edges;
  unclocked input updates every sample; channels above 8 are ignored.
- Writer channel counts and exact 0 V / 10 V levels.

## 9. Conformance checklist

Each module's manual states:

- which ports are buses and the channel count it emits (8 or 9);
- for each bus input, transparent or latching; what it does with no CLK; the
  internal clock's range;
- which clock it forwards to its output;
- which numeric interpretation it uses, and the conversion range if it
  converts.

## 10. Module roles and roadmap

| Role (hardware counterpart, reference only) | Kind | Here |
|---|---|---|
| Converter: ADC in, DAC out (Drezno II) | transparent; sample-and-hold when clocked | **Converter**, with independent ranges, trims and clock policies; [contract](../vcv/bytebus/docs/CONVERTER.md). Vastish also reads/exports its own signals |
| Format and destination adaptation (original extension) | latching, explicit clock source | **Adapter**: CZ/Yamaha/signed codecs, routing and masks, gate/trigger/CV outputs; [contract](../vcv/bytebus/docs/ADAPTER.md) |
| Bit logic: invert, toggle, AND mask (Lipsk, Gera) | transparent | **Lookup2** Logic: all sixteen two-input functions on whole words, on A's clock; [contract](../vcv/bytebus/docs/LOOKUP2.md). Vastish LANE A/B + FUN, with optional per-FUN BIT normalization, still composes logic inside a voice |
| Transfunction: byte → byte through a shape, with shape and phase CV (Jena) | transparent; optional explicit ramp follower | **Lookup**: original generated families, JUMP shape steps from a bus bit, and Vastish/OTIS table import; [contract](../vcv/bytebus/docs/LOOKUP.md). **Vastish BUS**: gate-free pre-amp DSP and stateful transforms; [contract](../vcv/vastish/BUS_SPEC.md) |
| Counter / phase accumulator (Erfurt) | latching, internal clock | **Counter**: increment from INC or a bus, modulus, Loop/One-shot/Pendulum, seed and carry, with Register's clock ownership; [contract](../vcv/bytebus/docs/COUNTER.md). **Counter 2**: a phase at N/D of the clock, or of a ratio word from Lookup's Ratio family; [contract](../vcv/bytebus/docs/COUNTER2.md) |
| Single-word register | latching, clock source selectable | **Clocked Register**, prototype in [`vcv/bytebus`](../vcv/bytebus) |
| Pipeline delay with loop and scramble (Rostock) | latching | **Pipeline**: a 1–64 edge delay, a per-bit SPREAD and a LOOP that repeats the last DELAY words, with Register's clock ownership; [contract](../vcv/bytebus/docs/PIPELINE.md) |
| Commutator: 2:1 switch, bit poke, reclock (Poczdam) | transparent + reclocker | **Switch**: Select, Bits and Mix of A and B under a control word C; follows A's clock or its own; [contract](../vcv/bytebus/docs/SWITCH.md) |
| Oscillator emitting a phase word (Berlin) | sender | Any Vastish voice (SAW, NONE chain) through BUS OUT |
| Spectral activity and band mask (original extension) | frame-clocked sender + held mask input | **Séance Bridge**: eight bands, pre-mask detection, hysteresis and gain slew; [contract](../vcv/seance/docs/BYTE_BUS.md) |

Implemented prototypes: `ByteBus.hpp`, Clocked Register, Counter, Counter 2, Switch, Pipeline, Lookup, Lookup2, Converter,
Vastish bus I/O with optional normalized FUNs, OTIS frame control/export and
the memoryless Vastish table compiler, and Séance Bridge spectral activity/masking.
Every role in the table now has an implementation.

## 11. Ratified decisions

Ratified as written on 2026-10-26. Changing one of these is a new spec version,
not an edit to v0.1.

1. Name **Harriot**; panel label **BUS**.
2. LSB-first channel order (§3).
3. Mode by channel count (§3). The alternative, a separate clock cable, loses
   the zero-skew property and costs a port on every module.
4. ±5 V full scale, offset binary (§6).
5. Latching modules default to an internal clock in Hz rather than the engine
   rate (§5).
6. Reserved channels 9–15 stay reserved in v0.1 (§3). Every bus reader ignores
   them, so a later version can assign one (§12) without changing what a bus
   input decodes. An input that reads the cable as plain channels, such as
   Machinemodule's TRIG in its per-track mode, would see the new line.

## 12. Extensions, not in v0.1

- Wide word: 16 channels as one 16-bit unclocked word. Conflicts with the
  "≥ 9 channels = clocked" rule; would need "exactly 16" as its own case.
- Channel 9 as RESET or strobe for counters and pipelines.
