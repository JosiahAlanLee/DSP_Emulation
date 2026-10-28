# Lattice

**ByteBusLab 2.13 · 26 HP · a 4 × 4 grid on two counters**

Contract for ByteBusLab 2.13. Two 4-bit up/down counters, H and V, on separate
clocks, select one of sixteen step knobs: V's low two bits are the row, H's the
column. Two jump inputs preset one counter from the other, as the
cross-coupled jam wiring of the Nonlinearcircuits Bindubba by Andrew Fitch
does, or from a byte bus. The whole state, V × 16 + H, is published on a
[byte bus](../../../docs/BYTE_BUS_SPEC.md) with the
[Clocked Register](MANUAL.md)'s strobe rules. What the Bindubba's build
document establishes, and what is inferred, is in
[`docs/NLC_BUS_SPEC.md` §7](../../../docs/NLC_BUS_SPEC.md#7-lattice-from-bindubba).
The panel, names and the bus side are original.

## What it is for

- **A 16-step sequencer.** With only a clock, V follows H's wrap and the steps
  run 1 … 16 in order. CV OUT plays the knobs; STEPS 1–8 and STEPS 9–16 carry
  sixteen one-hot gates.
- **A two-axis sequencer.** A second clock on V CLOCK moves the row on its own
  timing: two clocks at different rates, or a clock and a slower divider, walk
  the grid as a raster that drifts.
- **Semi-random jumps.** A trigger on JUMP H or JUMP V moves one counter to a
  place set by the other counter's state, which the Bindubba's documentation
  calls semi-random. Held, a Follow jump locks one counter to the other.
- **Jumps to a table.** BUS OUT → [Lookup](LOOKUP.md) → BUS IN, and a trigger on
  a jump input, moves to a table function of the current state.
- **Two slewed copies.** SLEW A and SLEW B each glide the CV at their own time
  under CV control, as the original's vactrol slews do.

## Controls and ports

| Item | Behavior |
|---|---|
| STEP 1–16 | Knobs, 0–100 % of CV RANGE. Row *r* (1–4, top to bottom) and column *c* (1–4, left to right) is step 4(*r* − 1) + *c*. |
| CV RANGE | Selector: 0–10 V (default), 0–5 V, ±5 V. |
| JUMP | Selector: Follow (default) or Load. See [Jumps](#jumps). |
| H SOURCE | Selector: Auto (H CLOCK jack, else Internal), H CLOCK jack, Internal, Engine rate. H's clock. |
| RATE | Internal H clock, 0.125–2048 Hz, default 8 Hz. Used only when Internal owns H's clock. |
| SEED | State 0–255 (V × 16 + H), default 0. RESET and LOAD load it. |
| LOAD | Loads SEED, restarts the internal clock phase and publishes the state. |
| SLEW A, SLEW B | Time constant 0.5–500 ms, logarithmic, default 0.5 ms. |
| H CLOCK, V CLOCK | Monophonic clocks; rising at 1 V, falling at 0.1 V. V CLOCK unpatched: H's wrap (see [Counting](#counting)). |
| DIR H, DIR V | Gates, same thresholds: that counter counts down while high. |
| JUMP H, JUMP V | Gates, same thresholds. |
| RESET | Trigger, same thresholds; loads SEED on its rising edge. |
| BUS IN | Jump targets: JUMP H takes bits 0–3, JUMP V bits 4–7. |
| A CV, B CV | Slew CV: each volt halves that slew's time, within 0.5–500 ms. |
| CV | The selected step knob in CV RANGE. |
| INV | −CV. |
| SLEW A, SLEW B (outputs) | CV through each slew. |
| BUS OUT | The state V × 16 + H: 9 channels with a strobe on each change, or 8 at Engine rate. |
| CLK OUT | The CLK channel of BUS OUT as a gate. |
| STEPS 1–8, STEPS 9–16 | 8 channels each: 10 V on the selected step's channel, 0 V on the others. Unclocked. |

The readout shows the state in hexadecimal, V's digit then H's: `3A` is V 3,
H 10, row 4, column 3, step 15. A light by each knob shows the selected step.

## Counting

- H counts on each acquisition edge of its clock, under the shared
  `src/engine/ClockOwner.hpp` rules. V counts on each V CLOCK rising edge.
- Each counter is 4 bits and wraps modulo 16, up, or down while its DIR gate
  is high. Only the low two bits, the counter's *position*, select the step;
  the upper two bits are carried state that a jump reads.
- **V normalled.** With V CLOCK unpatched, V counts once whenever H's position
  wraps: 3 → 0 counting up, 0 → 3 counting down. V counts in its own
  direction (DIR V), whichever way H wraps.

## Jumps

With BUS IN unpatched, the JUMP selector decides what a jump does.

| JUMP | JUMP H | JUMP V |
|---|---|---|
| **Follow** | H's position and upper bits both become V's position: H = 5 × (V's position). Steps 1, 6, 11, 16, the diagonal. | V's position becomes H's position with its two bits swapped, and V's upper bits become H's position. Steps 1, 10, 7, 16 for H's positions 0, 1, 2, 3. |
| **Load** | H's position becomes H's own upper bits, and its upper bits become V's position. | V's position becomes V's own upper bits, swapped, and its upper bits become H's position. |

- **Follow** is what the original's jam wiring settles to while its preset
  enable is held; **Load** is the jam values taken once, which a pulse too
  short to settle would give (`NLC_BUS_SPEC.md` §7.1). Which the hardware
  gives is not yet measured.
- In Follow, a jump held high keeps its counter following the other's
  position and ignores its own clock edges (for V unpatched, H's wraps); on
  release it counts on from where it is. While both are held, each keeps what
  it took on the sample the second rose.
- In Load, a jump acts on its rising edge only, and the counter keeps counting
  while it is held.
- **BUS IN.** Patched, it replaces both modes: a rising JUMP H loads H from
  bits 0–3 of BUS IN's current word, a rising JUMP V loads V from bits 4–7,
  the same layout as BUS OUT. The word is read at the jump's edge, not held to
  BUS IN's CLK.
- Two jumps on one sample are both evaluated from the state before them.

## Order within a sample

1. RESET or LOAD loads SEED. On that sample, V's edge and the jumps are
   ignored. H's clock edges are ignored until the seed has been published and
   a low sample has followed, as on every ClockOwner module.
2. H counts on its edge, then V on its own edge or H's wrap.
3. The jumps, from the state the clocks left.

Every change, and every counting edge, publishes the state once. A strobe never
follows a strobe: a change on the sample after one is published on the next.
Engine rate counts H every sample and publishes 8 channels with no strobe.
RESET or LOAD publishes the seed after a low sample if CLK was high, as on
every ClockOwner module. Each cable hop costs one engine sample.

## Slew

Each slew is a one-pole lag on CV OUT. Its time constant is the knob's,
0.5 ms to 500 ms on a logarithmic scale, halved for each volt at its CV input
and held within 0.5–500 ms, the original's vactrol range from lit to dark: more
CV, less slew. The CV itself passes a vactrol-like lag, about 5 ms as it rises
and 50 ms as it falls, so a stepped slew CV changes the glide smoothly. Both
slew outputs start at CV OUT's value.

The vactrol's curve and the time constant at each knob position are
provisional pending measurement (`NLC_BUS_SPEC.md` §9.2).

## Saving and reset

Rack saves the controls and the state, as `"state"`, bounded to 0–255. Reload
publishes the saved state; it does not resume the internal clock phase or the
slews, which restart at CV OUT. Rack's Initialize restores the defaults
(Follow, 0–10 V, SEED 0, slews 0.5 ms, knobs at 0) and state 0.

## First patch

1. **Lattice** with nothing patched: Internal at 8 Hz. The step lights walk
   1 … 16 and the readout reads `00`, `01`, `02`, `03`, `14`, `15` …: H moves
   on every step and V every fourth.
2. Set the sixteen knobs and CV → an oscillator's V/OCT: a 16-step sequence.
   STEPS 1–8 → Core **Split** gives a gate per step.
3. A slower clock (a Counter's CARRY, or a clock divider) → V CLOCK: the row
   now moves on its own timing.
4. A trigger every few beats → JUMP H, JUMP set to Follow: the sequence lands
   on the diagonal. Switch to Load and hear the other result.
5. BUS OUT → Lookup BUS IN, Lookup BUS OUT → Lattice BUS IN, a trigger → JUMP H
   and JUMP V: each trigger jumps to the table's word for the current state.
6. SLEW A at noon and an LFO into A CV: the glide opens and closes.

## Not established

From `NLC_BUS_SPEC.md` §7.4, pending measurement on a hardware Bindubba
(§9.2): whether a jump gives the Follow or the Load result; which counter is
the horizontal one (Lattice assumes the main counter is H); which knob is which
step (Lattice numbers them row by row); the CV output's top; the vactrol's
curve.

## Validation

Portable tests (`tests/engine_test.cpp`): one tick from every state under
every input combination (clocks, directions, previous and present jump levels,
Follow, Load, BUS IN) against a reference transition; Follow and Load against the
schematic's jam wiring for all 256 states, iterated to its settled values and
taken once; counting and wraps in both directions; V normalled to H's wrap,
with DIR H; V CLOCK and DIR V; held Follow jumps following and ignoring their
clocks, release, both held; Load once per rising edge with clocks still
counting; BUS IN jumps on rising edges; RESET priority over V edges and jumps;
every change published, no strobe after a strobe, deferred changes, under
random clocks, jumps, resets, directions and modes; the internal clock at
44.1/48/96 kHz; slew time constants at three knobs and four CVs including the
limits, and the control's rise and fall.

Rack API tests (`tests/module_test.cpp`): real ports and lights, the 16-step
raster at Engine rate with CV, INV, both gate cables and the step lights; all
three CV ranges; Follow, Load and BUS IN jumps through the panel; slew times
and slew CV through the ports; persistence of controls and state with bounded
restore, Initialize; a Counter → Lattice BUS IN chain whose strobe drives both
jumps while Lattice's own H clock runs, in random module order, identical at
44.1/48/96 kHz.

Software tests do not establish physical clock transport.
