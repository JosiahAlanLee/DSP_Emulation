# Namagiri

**ByteBusLab · 16 HP · the Rogers–Ramanujan continued fraction's convergents as clocked CV**

Contract for ByteBusLab 2.18. The Rogers–Ramanujan continued fraction is

```
R(q) = q^(1/5) / (1 + q / (1 + q² / (1 + q³ / (1 + ...))))
```

Srinivasa Ramanujan gave it, and the Rogers–Ramanujan identities it belongs to,
in his 1913 notebooks; the module is named for Namagiri, the family goddess in
whose visions Ramanujan said his mathematics came to him. The finite
convergents `R_n(q) = q^(1/5) / (1 + q/(1 + q²/(... 1 + q^n)))` and their
controls follow HydraPy's `namagiri.py`
([PR #93](https://github.com/JosiahAlanLee/hydrapy/pull/93), the companion to
the `ramanujancf` picture operator), evaluated bottom-up with the principal
branch of `q^(1/5)`; the parameter bound `|q| ≤ .97` and the denominator floor
are the operator's.

Namagiri steps `n` from 1 to DEPTH on its clock and holds `R_n(q)` on its
outputs, then wraps: each cycle is a phrase built into the math. The
convergents swing widely at first and settle towards `R(q)`; near `|q| = 1`
they settle slowly and wander, near 0 they settle at once. Moving `q` — RADIUS
and ANGLE — reshapes the whole phrase.

## What it is for

- **A phrase from one knob.** DEPTH 12, ANGLE around 0.37 turns, a clock into
  CLOCK: the convergents step out a sequence of complex values that starts
  with big moves and converges, published on REAL, IMAG, MAG, PHASE and
  DELTA.
- **An envelope that opens when the phrase settles.** CONVERGED goes high at
  the first step whose change is smaller than THRESH and stays until the
  wrap; patch it into an envelope or a VCA so each cycle's tail rings.
- **A quantizer melody.** PHASE into a quantizer into a VCO: the angle of each
  convergent becomes a pitch, and the whole phrase transposes as ANGLE turns.
- **A phase word.** BUS OUT carries each convergent's angle as a byte (0x80 is
  angle 0, exactly spec §6's bipolar reading of a fraction of π), so a Lookup
  or a VCO reads the phrase directly. EOC marks the wrap, a rhythm of one
  trigger per cycle.
- **Slow wandering.** RADIUS near 0.97, ANGLE near 1/2: the convergents take
  many steps to settle and the tail wanders, and DELTA shows how little is
  left to move.

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Selector: Auto (CLOCK jack, else internal), CLOCK jack, Internal, Engine rate. Clock ownership is the Clocked Register's. |
| RATE | Internal clock, 0.125–2048 Hz. Used only when Internal owns the clock. |
| RADIUS | `|q|`, 0–0.97, default 0.8. |
| ANGLE | arg q in turns, 0–1, wrapping. |
| DEPTH | The cycle length, 1–32, default 12. |
| THRESH | The CONVERGED threshold, in the DELTA output's volts, 1 mV–10 V logarithmic, default 0.1 V. |
| R CV | ±5 V spans `|q|` 0–0.97 (10 V full range). |
| A CV | ±5 V spans ±0.5 turns (10 V one full turn). |
| D CV | ±5 V spans ±16 steps (10 V all 32). |
| CLOCK | Monophonic clock; rising at 1 V, falling at 0.1 V. |
| RESET | Monophonic trigger, same thresholds; back to step 1 on its rising edge. |
| BUS IN | A byte word that sets n directly: the low bits index the step, modulo DEPTH, so a 5-bit address drives 1–32 at DEPTH 32 as in Fan. An unclocked bus sets n every sample (the outputs track it); a clocked one on its CLK edge. A bus update on the same sample as a clock edge lands after the edge. |
| REAL, IMAG | Re(R(q)) and Im(R(q)), 5 V per unit, clipped to ±5 V. |
| MAG | `|R(q)|`, 5 V per unit, clipped to 0–10 V. |
| PHASE | arg R(q), ±5 V for ±π. |
| DELTA | This step's change, 5 V per unit, clipped to 0–10 V. |
| CONVERGED | Gate, 10 V once a step's change falls below THRESH, until the wrap clears it. |
| EOC | One-engine-sample trigger at each wrap. |
| BUS OUT | 9 channels, exact 0/10 V bits: the phase word, the angle as a signed fraction of π in offset binary (0x00 is −π, 0x80 is 0, 0xFF is just below +π); a DAC stage reads `(n − 128)/128 · π` back out. Engine mode emits 8 channels. |

The readout shows the current step, 1–32. Eight lights show the phase word's
bits, MSB at the left.

## The convergents

For one `q` and DEPTH `d` the module holds `R_1(q) … R_d(q)`, computed with the
finite definition above: for each n the tail is built from the inside out,

```
tail = 1
tail = 1 + q^k / tail   for k = n down to 1
R_n  = q^(1/5) / tail
```

A tail smaller than 1e-10 is replaced by 1e-10, as in the `ramanujancf`
operator, so every input is defined and finite — with `|q| ≤ 0.97` the
smallest reachable tail magnitude is about 6.5e-3, so the floor never fires
in practice. At `q = 0` every convergent is 0.

DELTA is the change the outputs just made: `|R_n − previous|`, where
*previous* is the last published value. On the first step of a fresh module
or after RESET it is `|R_1 − 0| = |R_1|`, the first move out of silence; at
the wrap it is `|R_1 − R_d|`, the jump the outputs make as the cycle restarts.
For n ≥ 2 it is `|R_n − R_(n-1)|`, HydraPy's `convergent_controls` delta.

CONVERGED is a latch: it falls at the wrap, then rises again on the first
step whose change is below THRESH (on the wrap's own step, if the wrap jump
is small enough, it rises again immediately — a settled phrase stays
converged). RESET clears it.

The table rebuilds only when q or DEPTH changes, never per sample; turning
RADIUS, ANGLE or DEPTH re-holds the current step at once (without a strobe),
so the phrase reshapes live. Changing DEPTH clamps the current step to the
new length.

## Clock, reset and strobe timing

Identical to the Clocked Register's clock ownership and
[clock and reset timing](MANUAL.md#clock-ownership) through the shared
`src/engine/ClockOwner.hpp`: RESET publishes step 1 with acquisition
suppressed for that strobe and the low sample after it, RESET priority over a
simultaneous edge, and one-sample strobes with a mandatory low sample. A bus
update on BUS IN publishes with the same strobe rules, deferred past a low
sample rather than following a strobe directly.

## Saving and reset

Rack saves the controls, the current step, the last published value and the
CONVERGED latch, so a reloaded Namagiri continues the same cycle. Values are
bounded on load. Reload publishes the saved step; it does not resume the
fractional internal clock phase. Initialize restores RADIUS 0.8, ANGLE 0,
DEPTH 12 and step 1.

## First patch

1. Add **Namagiri** from **ByteBusLab**. A **Clock** (or Gorgon's CLOCK) into
   CLOCK, DEPTH 12, ANGLE about 0.37 turns.
2. **PHASE** → **Quantizer** → **VCO** → your mixer: the angle of each
   convergent becomes a pitch, stepping out a melody that is entirely the
   mathematics of R(q).
3. **CONVERGED** into an envelope's gate: the envelope opens at the first
   settled step of each cycle and falls at the wrap. Raise THRESH to open
   earlier, lower it to wait for real convergence.
4. **EOC** into a delay's clock or a sequencer's RESET, so other modules
   count Namagiri's cycles.
5. **REAL** and **IMAG** into a Cartesian-to-polar converter, or into the x/y
   of a scope or a vector mixer: each cycle draws the path the convergents
   take as they approach R(q).
6. Turn RADIUS towards 0.97 and ANGLE towards 1/2: the phrase wanders for the
   whole cycle before it settles. Turn ANGLE with CV from a slow LFO and the
   whole phrase rotates with it.

## Validation

Portable tests (`tests/namagiri_test.cpp`): the convergents and their
controls against HydraPy's `namagiri.py` to 1e-9 over a committed grid of q
(radii 0, .05, .5, .9, .97 at five angles, depths 1–32, regenerated with
`tools/generate_namagiri_reference.py --check`); q = 0 and the guarded
denominators; the phase-word encoding against spec §6; clock stepping, wraps,
EOC and held outputs; RESET priority; the CONVERGED latch; BUS IN step words;
DEPTH and q changes; internal and Engine clocks at 44.1/48/96 kHz.

Rack API tests (`tests/module_test.cpp`): real ports; output bounds; the
phase word 0x80 at angle 0; EOC and the CONVERGED gate; persistence and
bounded restores; Initialize; RESET replay; the BUS IN step word; and a
Counter CARRY → Namagiri CLOCK chain through one-sample cables in random
module order at 44.1/48/96 kHz.

Software tests do not establish physical clock transport.

## First sample after Initialize or restore

A fresh instance and Initialize publish step 1 with delta `|R_1|`. Restore
starts from the saved step and its saved previous value, so DELTA on the
first publication is the change since the save. The first sample publishes
without acquiring from an already-high clock. A RESET already high on that
sample takes priority and loads step 1 instead; holding it high does not
reload or block later clock edges. A BUS IN word already present tracks
immediately; an unclocked bus overrides the step every sample while it is
patched, including the first.
