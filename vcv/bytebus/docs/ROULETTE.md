# Roulette

**ByteBusLab 2.16 · 14 HP · a phase-locked loop that spins, coasts and divides**

Contract for ByteBusLab 2.16. A phase-locked loop drives a 12-bit binary
counter. With TRACK patched it follows the input's frequency, loosely; with a
gate in ROULETTE it spins up, and when the gate falls it coasts to rest at a
rate the loop filter sets, like a roulette wheel; with DIVIDE patched the
counter divides that input instead. The count is published as two overlapping
[byte buses](../../../docs/BYTE_BUS_SPEC.md), LO and HI, a strobe on every
count, and the count at each rest on a third, RESULT. The design reference is
the Nonlinearcircuits Wangernumb by Andrew Fitch; how Roulette relates to it,
and what its schematic establishes, is in
[`docs/NLC_BUS_SPEC.md` §6](../../../docs/NLC_BUS_SPEC.md#6-roulette-from-wangernumb).
The panel, names and the bus side are original.

## What it is for

- **A spinning divider.** A gate into ROULETTE: LO's eight bits spin, faster
  and slower octaves of the same motion, and slow to a stop when the gate
  ends. FILTER is the friction: at its minimum the wheel stops dead, at its
  maximum it coasts for about a second.
- **One random byte per spin.** RESULT publishes LO as the VCO comes to rest,
  with one strobe: a byte that depends on the spin's length and the friction,
  on a clock that marks the end of the spin.
- **A burst generator.** VCO OUT and the low count bits run while the gate is
  high; RANGE sets how fast.
- **A frequency tracker.** TRACK from an oscillator or audio: the VCO follows
  it, through the XOR comparator's ripple ("very badly behaved" tracking, as
  the original's documentation says), or cleanly through the edge comparator.
  LO and HI are then sub-octaves of the tracked pitch.
- **A divider.** DIVIDE takes any clock and LO and HI divide it by 2 … 4096,
  while the VCO runs separately.

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Selector: Auto (DIVIDE jack if patched, else the VCO), DIVIDE jack, VCO. The counter's clock. |
| COMPARATOR | Selector: XOR (default, the hardware's), Edge. |
| SCALE | Selector: ×0.1, ×1 (default, the hardware's 2 µF), ×10, on the loop filter's capacitor. |
| RANGE | The VCO's maximum, 1 Hz – 20 kHz, logarithmic, default 2 kHz. |
| FILTER | The loop filter's series resistance, 0–100 kΩ, default 50 kΩ: the friction. |
| TRACK | Any signal; a comparator at 1.2 V with ±10 mV hysteresis. Unpatched, it reads low. |
| ROULETTE | A CV or gate that replaces the comparator while patched: 12 V is the VCO's full speed, 10 V five-sixths. TRACK does nothing while it is patched. |
| DIVIDE | Clock; rising at 1 V, falling at 0.1 V. |
| RANGE CV | 1 V per octave on RANGE, through a vactrol-like lag. |
| RESET | Trigger, same thresholds: count 0, VCO restarted low, published. |
| LO BUS | Count bits 0–7 (/2 … /256): 9 channels, a strobe on every count. |
| HI BUS | Count bits 4–11 (/32 … /4096), same strobe. HI's bits 0–3 are LO's bits 4–7. |
| RESULT BUS | LO as the VCO comes to rest: 9 channels, one strobe per rest. |
| VCO | The VCO's square, ±5 V, band-limited. |
| CLK OUT | The count strobe as a gate. |

The readout shows the VCO's frequency, or REST while it is stopped. Twelve
lights show the count, /2 at the left. Twelve mono gates are LO and HI through
Core Split.

## The loop

Voltages in the loop are fractions of the hardware's 12 V supply, 0 … 1.

**Comparator.** On each sample the comparator gives *u*:

- XOR: *u* is 1 while TRACK's comparator and the VCO's output differ, else 0.
  Unpatched TRACK reads low, so the XOR's output is the VCO's own square.
- Edge (the 4046's second comparator): a rising TRACK edge moves it toward
  driving *u* high, a rising VCO edge toward driving it low, and between the
  two it leaves the filter floating: no current, the filter holds.
- ROULETTE patched: *u* = ROULETTE / 12 V, clamped to 0 … 1.

**Filter.** A lead-lag network: the series resistance *Rs* (FILTER, plus the
comparator's 1 kΩ when ROULETTE is unpatched) into 47 kΩ and *C* = 2 µF × SCALE
in series to ground:

```
i      = (u − vC) / (Rs + 47 kΩ)
dvC/dt = i / C
VCOIN  = vC + i · 47 kΩ
```

The capacitor is integrated exactly over each sample (an exponential step),
so for a ROULETTE voltage that changes only between samples the result is the
same at every engine rate.

**VCO.** Frequency = RANGE × 2^(RANGE CV) × VCOIN: linear from zero, stopping
below VCOIN 0.01 with its phase and output held, and at most half the engine
rate. RANGE CV passes a lag of 5 ms rising and 50 ms falling.

**The coast.** With ROULETTE at 10 V for long enough, the capacitor nears
0.83. When the gate falls, VCOIN drops at once by the lead fraction
47k / (*Rs* + 47k) of the capacitor's voltage, then decays with
τ = (*Rs* + 47 kΩ) × *C*, 94–294 ms at ×1. At FILTER 0 the VCO stops dead; at
100 kΩ it drops to about two-thirds speed and comes to rest after about
1.2 s. SCALE ×10 makes that twelve seconds; ×0.1 a tenth of a second.

**Resting state.** With nothing patched the VCO is stopped with its output
low: the XOR gives 0 and nothing charges the filter. A VCO stopped high moves
on to low and rests there. Once TRACK has set it running, the XOR's own square
keeps it running after TRACK is removed when FILTER is high enough to hold
VCOIN above the stop through the square's low half; at FILTER 0 it falls back
to rest. ROULETTE patched and at 0 V always lets it come to rest.

## The counter

- The counter counts rising edges of DIVIDE, or of the VCO's output, which
  passes to ClockOwner as a jack clock. Every count is strobed: the VCO is held
  to half the engine rate, the fastest a strobe and its low sample allow.
- Bit *k* divides by 2^(*k* + 1). The hardware's ripple counters settle in a
  fraction of a microsecond; here all twelve bits change on one sample.
- RESET (*original*; the hardware has none) sets the count to 0 and the VCO to
  the start of its low half, so a stopped VCO stays stopped and a running one
  counts again half a period later. ClockOwner's rules apply: RESET priority
  over a simultaneous edge, the reset count published with a strobe, and no
  count on that strobe or the low sample after it.
- **RESULT.** On the sample the VCO stops, RESULT takes LO's word and
  publishes it with one strobe (deferred a sample if a strobe is high). RESULT
  holds between rests. A stop is any fall of VCOIN below 0.01, so a VCO
  hovering at the threshold, as tracking a very low frequency can, gives a
  RESULT each time it stops.

## Saving and reset

Rack saves the controls, the count (`"count"`, 0–4095), RESULT (`"result"`),
the capacitor (`"filter"`, 0–1) and the VCO's phase (`"phase"`). Reload
publishes the count and resumes the loop from the saved capacitor and phase,
so a spinning wheel keeps coasting; RANGE CV's lag restarts at 0 V. Rack's
Initialize restores the defaults (Auto, XOR, ×1, 2 kHz, 50 kΩ), count 0,
RESULT 0 and the VCO at rest.

## First patch

1. **Roulette**, a gate (a button, or a slow LFO's square) → ROULETTE. Hold
   it: the count lights spin. Release: they slow and stop, and RESULT pulses
   once.
2. LO BUS → Core **Split**: eight gates from /2 to /256 that spin and stop
   together. HI BUS for slower ones.
3. RESULT BUS → **Lookup** or a Register: one new byte per spin.
4. Turn FILTER: at the minimum the wheel stops with the gate; at the maximum
   it coasts. SCALE ×10 for long, slow coasts.
5. Unpatch ROULETTE and patch an oscillator into TRACK at 100 Hz: VCO OUT
   follows it with a wobble. Switch COMPARATOR to Edge for a clean lock.
6. A clock into DIVIDE: LO and HI divide it, and VCO OUT is free.

## Not established

From `NLC_BUS_SPEC.md` §6.4, pending measurement on a hardware Wangernumb
(§9.1): the 4046 VCO's law against VCOIN (Roulette assumes linear from zero);
the vactrol's range and lag, which set RANGE and RANGE CV's response; whether
the resting state, the coast and the self-modulated tracking behave as above.

## Validation

Portable tests (`tests/engine_test.cpp`): every count bit an exact division
of DIVIDE, LO and HI; the resting state, a stopped-high VCO coming to rest, and
the XOR's own square keeping a running VCO going at high FILTER but not at 0;
the spin and coast at three FILTER settings, VCOIN against the closed form
within 1e-9 and the stop within a sample of it, one RESULT per stop with the
count at rest, at 44.1/48/96 kHz; the filter against a fine integration under a
continuously varying ROULETTE; the VCO at half the engine rate with every
count strobed; ROULETTE replacing the comparator; XOR and edge tracking from
50 Hz to near RANGE at three engine rates; RANGE CV at one volt an octave and
its lag; RESET priority and strobe gaps under random DIVIDE edges and resets.

Rack API tests (`tests/module_test.cpp`): real ports and lights against an
engine fed the same inputs through rest, DIVIDE, a spin and coast with one
RESULT, a TRACK lock, RANGE CV, the edge comparator with another SCALE and
FILTER, and RESET; persistence of the count, RESULT, capacitor and phase with
bounded restore, Initialize; a Counter → DIVIDE chain past the 12-bit wrap in
random module order, identical at 44.1/48/96 kHz.

Software tests do not establish physical clock transport.
