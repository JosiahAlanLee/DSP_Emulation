# Fan

**ByteBusLab 2.12 · 16 HP · one address, 32 places**

Contract for ByteBusLab 2.12. A 5-bit address routes COM IN to one of 32
outputs and one of 32 inputs to COM OUT, both at once, on two 16-channel
polyphonic cables per direction. The address is an internal up/down counter of
1–32 steps, with the [Clocked Register](MANUAL.md)'s clock ownership and strobe
timing, or bits 0–4 of BUS IN, followed transparently under the
[byte bus](../../../docs/BYTE_BUS_SPEC.md) rules (§5). The design reference is
the Nonlinearcircuits 32:1 by Andrew Fitch, itself after the 32-stage
counter/multiplexer of Julian Driscoll and Warren Burt; how Fan relates to it,
and what its build document establishes, is in
[`docs/NLC_BUS_SPEC.md` §8](../../../docs/NLC_BUS_SPEC.md#8-fan-from-321).
The panel, names and the bus side are original.

## What it is for

- **A 32-step gate sequencer.** COM IN unpatched is 10 V, so OUT 1–16 and
  OUT 17–32 are one-hot gates that walk with the count: two cables into Core
  Split, or straight into polyphonic trigger inputs. LENGTH sets the cycle and
  EOC marks it.
- **A 32-step CV sequencer.** Up to 32 voltages into IN 1–16 and IN 17–32
  (Core Merge of constants, or any polyphonic CV); COM OUT plays them in
  order.
- **Scanning audio.** With CLOCK from an oscillator, COM OUT steps through 32
  inputs at audio rate, or COM IN is chopped across 32 outputs.
- **An addressed switch.** BUS IN takes the address from any word: Counter for
  other step sizes, Lookup's Sequence or Walsh families for other orders,
  Pipeline for a delayed copy of another Fan's path.
- **Sample and hold across 32 lanes.** UNSELECTED Hold keeps what each output
  last carried: COM IN is sampled onto whichever output is selected.

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Selector: Auto (CLOCK jack, else Internal), CLOCK jack, Internal, Engine rate. The internal counter's clock. There is no Bus CLK choice: a patched BUS IN replaces the counter. |
| DIR | Selector: Up (default), Down. The DIR gate inverts it while high. |
| UNSELECTED | Selector, UNSEL on the panel: Zero (default) or Hold. See [Routing](#routing). |
| GLITCH | Latching button with a light. See [GLITCH](#glitch). |
| RATE | Internal clock, 0.125–2048 Hz, default 8 Hz. Used only when Internal owns the clock. |
| LENGTH | Integer 1–32, default 32. The counter visits steps 1 … LENGTH. |
| SEED | Step 1–32, default 1. RESET and LOAD load it. |
| LOAD | Loads SEED, restarts the internal clock phase and publishes the address. |
| CLOCK | Monophonic clock; rising at 1 V, falling at 0.1 V. |
| RESET | Monophonic trigger, same thresholds; loads SEED on its rising edge. |
| DIR (input) | Gate, same thresholds. |
| BUS IN | Address byte bus: bits 0–4 of its word; bits 5–7 are ignored. Patched, it replaces the internal counter. |
| COM IN | Mono. Routed to the selected output. Unpatched, 10 V. |
| IN 1–16, IN 17–32 | Polyphonic; channel *k* is input *k* + 1 of that half. Missing channels read 0 V. |
| BUS OUT | The address as a word, bits 5–7 zero: 9 channels with a strobe on each step, or 8 at Engine rate or when following an unclocked BUS IN. |
| CLK OUT | The CLK channel of BUS OUT as a gate. |
| EOC | One-engine-sample pulse on each counter wrap. |
| COM OUT | The selected input. |
| OUT 1–16, OUT 17–32 | Always 16 channels each. |

Five LEDs show the address in binary, bit 4 at the left. The readout shows the
step 1–32, or BUS and the step while BUS IN owns it. Steps are numbered 1–32
on the panel and 0–31 on the bus.

## Address

**Counting.** With BUS IN unpatched, each acquisition edge moves the count one
step, up or down (DIR, inverted while the DIR gate is high):

- Up: 0, 1 … LENGTH − 1, then 0 with EOC.
- Down: LENGTH − 1 … 0, then LENGTH − 1 with EOC.
- A count outside 0 … LENGTH − 1 (a SEED past LENGTH, or LENGTH lowered below
  the count) is published as it is and returns to the range on the next edge:
  to 0 counting up, to LENGTH − 1 counting down, with EOC either way.
- LENGTH and DIR changes act on the next edge.

**Following.** With BUS IN patched, the address is bits 0–4 of BUS IN's word at
its sample instants: every engine sample on an unclocked bus, each CLK rising
edge on a clocked one, held between. Fan is then transparent: BUS OUT keeps
BUS IN's clocked or unclocked mode and passes its CLK level unchanged. The
counter keeps its count; RESET, LOAD and the clock do nothing to it. When
BUS IN is unpatched, the held count is published at once, with a strobe, as a
load is.

## Routing

On every sample the address selects one output channel and one input channel
(two or none, for one sample, under [GLITCH](#glitch)).

- The selected output carries COM IN.
- Unselected outputs are 0 V in **Zero**, as the original's off analogue
  switches are into an ordinary input. In **Hold** each keeps the last value it
  carried while selected; the held values start at 0 V and are not saved with
  the patch.
- COM OUT is the selected input.
- Both directions run at once from the same address, which the original's
  single in/out jack cannot do.

## GLITCH

On the original, bit 4 enables the second multiplexer directly and the first
through an op-amp inverter, which is late. GLITCH models that for one engine
sample on any address change that flips bit 4, from the counter or from
BUS IN:

- **Bit 4 rises** (up 16 → 17; down 1 → 32): both halves are selected at the
  new low four bits. Outputs 1 and 17 both carry COM IN on the way up, 16 and
  32 on the way down, and COM OUT is the mean of the two inputs, as the two
  470 Ω paths would join them. The count-17 trigger on out 1 is the one the
  original's documentation reports.
- **Bit 4 falls** (up 32 → 1; down 17 → 16): neither half is selected. The
  outputs follow UNSELECTED and COM OUT is 0 V.

The down-count overlap and both dropouts follow from the same circuit and are
not documented for the hardware. Off, every sample selects exactly one channel.

## Clock, reset and strobe timing

The internal counter runs the shared `src/engine/ClockOwner.hpp`, as Counter
does: Auto selection by connection, an initially high clock counting as its
first edge, one-sample strobes with a mandatory low sample, RESET priority over
a simultaneous edge, the seed published after RESET or LOAD with acquisition
suppressed for that strobe and the low sample after it. Engine rate counts every
sample and emits no clock channel. Each cable hop costs one engine sample.

## Saving and reset

Rack saves the controls and the internal count, as `"count"`, bounded to 0–31.
Reload publishes the saved count; it does not resume the fractional internal
clock phase. Rack's Initialize restores the defaults (Up, Zero, GLITCH off,
LENGTH 32, SEED 1) and count 0.

## First patch

1. **Fan** with nothing patched: Internal at 8 Hz. The LEDs count, the readout
   walks 1 … 32, and EOC pulses once per cycle.
2. OUT 1–16 → Core **Split**: sixteen gates, one after another, then a gap of
   sixteen steps while OUT 17–32 has the gate. Set LENGTH 16 to stay in the
   first half.
3. A **Counter** BUS OUT → Fan BUS IN, INC 3, MOD 32: the gate moves three
   steps at a time on the Counter's clock. Unpatch it and Fan resumes its own
   count.
4. Sixteen voltages into IN 1–16 (VCV **Merge** of constants, or a polyphonic
   CV module) and COM OUT → an oscillator's V/OCT: a 16-step pitch sequence.
5. UNSELECTED Hold, an LFO into COM IN: thirty-two stepped copies of it, each
   sampled as the address passes.
6. GLITCH on with LENGTH 32: out 1 fires twice per cycle, once in place and
   once with out 17.

## Validation

Portable tests (`tests/engine_test.cpp`): every (address, LENGTH, direction)
through the step function, with EOC exactly on wraps and out-of-range counts
returning to range; the selection for every (previous, address) pair with and
without GLITCH; the full cycle at Engine rate for six lengths, both directions
and the DIR gate; routing at every address with COM IN unpatched and patched,
Zero and Hold; GLITCH's two overlaps and two dropouts per direction, with COM
OUT's mean; BUS IN addressing for all 256 unclocked words and a clocked word
with its CLK passed through, RESET ignored while following and the held count
published on release; RESET with a seed past LENGTH; strobe gaps under random
clocks, resets, lengths and direction gates; the internal clock at
44.1/48/96 kHz.

Rack API tests (`tests/module_test.cpp`): real ports and lights, both 16-channel
cables, short input cables reading 0 V, the one-hot counter mode, Hold and the
DIR gate, persistence of controls and count with bounded restore, Initialize,
GLITCH through the panel; Counter → Fan BUS IN through one-sample cables in
random module order, identical at 44.1/48/96 kHz.

Software tests do not establish physical clock transport.
