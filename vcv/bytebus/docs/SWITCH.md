# Switch

**ByteBusLab · 12 HP · two words in, one out, under a third**

Contract for ByteBusLab 2.6. Implements the
[byte bus](../../../docs/BYTE_BUS_SPEC.md) rules for a transparent module (§5),
with an optional own clock that makes it a reclocker. A control word C decides
how words A and B combine: **Select** passes A or B whole, **Bits** takes each
bit from A or B, and **Mix** crossfades from A to B. The hardware role reference
is a binary commutator (Poczdam): a 2:1 switch, bit poking and reclocking.
Nothing here reuses manufacturer names, tables or behaviour beyond that public
role; the per-bit selection by a word, the crossfade and the word knobs are
original.

## What it is for

- A/B patterns: two rhythm words (Counter → Lookup Rhythm, twice) into A and
  B, and a slow bit into C. Counter bit 3 into C with SEL BIT 3 plays A for
  eight steps and B for eight; a gate into C (Gorgon's FILL bit through Split,
  say) plays B while it is high.
- Muting and forcing bits: B unpatched reads B WORD. In Bits, C = 0F and
  B WORD 00 clears the low four bits of A, and B WORD FF sets them. That is bit
  poking, with the mask from a knob or from another bus.
- An 8-bit VCA or crossfader: Mix with B WORD 80, which is 0 V in the bipolar
  reading, scales A's bipolar value by `(255 − C)/255` (B WORD 00 does the same
  to the unipolar word). With two buses it fades between them under a contour
  word: a counter's ramp or a Lookup curve.
- Reclocking: with its own CLOCK, Switch takes words from any clock domain and
  publishes them on its own edges, like Register but with three inputs.

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Selector: Auto (default), CLOCK jack, Follow A, Internal, Engine rate. Auto is the CLOCK jack when it is patched, else Follow A. See [Clocking](#clocking). |
| MODE | Selector: Select (default), Bits, Mix. See [Operations](#operations). |
| SEL BIT | Selector, Bit 0–7, default 0. The bit of C that Select reads. |
| RATE | Internal clock, 0.125–2048 Hz. Used only when SOURCE is Internal. |
| B WORD | Integer 0–255, default 0. B while the B input is unpatched. |
| C WORD | Integer 0–255, default 0. C while the C input is unpatched. |
| A | Byte bus: 8 data channels, or 9 with CLK. Its CLK is the output clock when following A. Unpatched, A is 00. |
| B | Byte bus. Patched, it replaces B WORD. |
| C | Byte bus. Patched, it replaces C WORD. A gate or trigger cable is bit 0, so a gate into C with SEL BIT 0 selects B while high. |
| CLOCK | Monophonic clock; rising at 1 V, falling at 0.1 V. |
| BUS OUT | The result: 9 channels with CLK when clocked, 8 when not. |
| CLK OUT | The CLK channel of BUS OUT as a gate. |

Hex display and eight LEDs show the published word, bit 7 at the left. The
light under CLK OUT shows word clocks. With only A patched and the defaults
(Select, SEL BIT 0, C WORD 00), BUS OUT is A.

## Operations

With `a`, `b` and `c` the words at a sample instant:

| MODE | Result |
|---|---|
| Select | `b` if bit SEL BIT of `c` is 1, else `a` |
| Bits | `(a AND NOT c) OR (b AND c)`: each bit from B where C has it, else from A |
| Mix | `(a · (255 − c) + b · c + 127) / 255`, integer division: `a` at C 00, `b` at C FF |

- Select is Bits with C's chosen bit spread to all eight.
- Mix is the byte nearest `a + (b − a) · c / 255`. The fraction `(b − a) · c / 255`
  is never exactly a half, so there are no ties and no rounding direction to
  choose. It moves monotonically from A to B as C rises.
- Words are read as `word` values (§6). Every reading in §6 is an affine map of
  the word, so Mix is also the crossfade of two bipolar or level readings, and
  80 is the bipolar zero to fade toward.

## Clocking

**Follow A (transparent).** The output is A's clock domain, as Lookup's
Map/Follow is:

- A clocked (nine channels): the operation is evaluated on each A CLK rising
  edge, from A's word and B's and C's raw lines on that sample, and held between
  edges. BUS OUT carries A's CLK level unchanged, however long it is high, so
  the result is published on the same sample as A's word reaches Switch.
- A unclocked or unpatched: the operation is evaluated every sample and BUS OUT
  has eight channels.
- B's and C's own clocks are ignored: they are read as raw lines, as Counter
  reads its BUS IN increment. A word changing on B between A's edges is
  invisible until the next one.

**CLOCK jack, Internal, Engine rate (reclocking).** Switch owns the clock with
the Clocked Register's [clock ownership](MANUAL.md#clock-ownership) and strobe
timing: one-sample strobes with a mandatory low sample, the internal clock at
RATE, and Engine rate as every sample with no clock channel. On each of its
edges it evaluates the operation from A's, B's and C's raw lines; A's clock is
ignored like the others. Taking the clock over (patching CLOCK in Auto, or
choosing one of these sources) publishes the current result first, as a LOAD
does on Register, with the following low sample before the next edge. There is
no RESET input: nothing is held that a reset would restore.

Changing SOURCE back to Follow A, or unpatching CLOCK in Auto, returns to A's
clock at once.

## Saving and reset

Switch holds no state beyond its last result, which it recomputes, so Rack saves
only its controls. Initialize restores Select, SEL BIT 0, words 00 and Auto.

## First patch

1. Two **Counters** on one clock: Counter 1 MOD 16 INC 1, Counter 2 MOD 16 INC 5.
   Counter 1 BUS OUT → Switch A and C; Counter 2 BUS OUT → Switch B. SEL BIT 3.
   BUS OUT shows Counter 1's count for 0–7 and Counter 2's for 8–15.
2. Put **Lookup** (Rhythm) after each Counter instead, with different PATTERN
   SEEDs, and Switch BUS OUT → Split → triggers: pattern A for half the cycle,
   pattern B for the other half.
3. Unpatch B, set MODE Bits, B WORD 00 and C WORD F0: the top four lanes are
   muted. Patch a slow Counter into C and they come and go.
4. MODE Mix, B unpatched, B WORD 80, a Counter in Pendulum into C, Switch BUS
   OUT → Converter: the Converter's DAC is A under a triangle VCA. (Pendulum is
   ByteBusLab 2.4.)

## Validation

Portable tests (`tests/engine_test.cpp`): every (A, B, C), all 16,777,216, for
Bits bit by bit, Select as Bits with C's bit spread, and Mix against the exact
crossfade with no ties; Mix endpoints and monotonicity; Follow A with a
clocked A (evaluation on the edge with raw B and C, holds, CLK level passed
through), an unclocked A, unpatched inputs and their words; Auto taking the
CLOCK jack over with a first publication, one strobe per edge and A's clock
ignored, then returning to Follow A; Internal at 16 Hz and Engine rate at
44.1/48/96 kHz.

Rack API tests (`tests/module_test.cpp`): real ports and lights, A passing
through by default, a gate into C, JSON restore and Initialize; two Counters
into A, B and C as an A/B commutator, and Mix as a VCA on a Counter, through
one-sample cables in random module order at 44.1/48/96 kHz, identical at every
rate; reclocking a Counter on its internal clock onto Switch's CLOCK jack with
strobe gaps.

Software tests do not establish physical clock transport.
