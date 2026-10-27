# Lookup2

**ByteBusLab · 16 HP · two words in, one out, through a shape**

Contract for ByteBusLab 2.9. A transparent module under the
[byte bus](../../../docs/BYTE_BUS_SPEC.md) rules (§5): `f[family][shape](A, B)`
→ byte, evaluated on A's clock. It takes over the bus spec's bit-logic roles
(Lipsk, Gera) as the Logic family, and adds arithmetic, a two-input fold and
two-word Walsh maps. JUMP steps SHAPE from logic, as on [Lookup](LOOKUP.md#jump).

Every family is computed, not stored, so Lookup2 has no table library and no
library identity: there is nothing to regenerate, verify or mismatch. The
two-input scan of the Via's SCANNER is the inspiration for treating a pair of
words as an address; the functions, orderings and maps here are original.

## What it is for

- **Bit logic on whole words.** A rhythm word AND a mask word mutes lanes; XOR
  with a slow Counter flips lanes in and out; NOT inverts. With B unpatched, B
  WORD is the second operand.
- **Byte arithmetic.** Two Converter ADC words through Mix or Ring are an 8-bit
  mixer or ring modulator; Max is SCANNER's "lighten"; A > B is a comparator
  whose output is a full byte, ready for BUS TRIG or a gate.
- **Terrain.** Fold reads (A, B) as a point and folds a surface through it:
  two Counters at different rates trace it, and SHAPE (or JUMP) deforms it.
- **Two-word Walsh.** Each output bit is the parity of chosen bits of A and B:
  a different binary rhythm for every pair, and one-to-one in each word.

## Controls and ports

| Item | Behavior |
|---|---|
| FAMILY | Selector: Logic (default), Arithmetic, Fold, Walsh. FAMILY CV adds one family per volt and clamps to the ends. |
| SHAPE | Circular 0–64; 64 returns to 0. SHAPE CV adds 64 positions per 10 V, and JUMP adds its steps. Logic and Arithmetic have sixteen shapes and pass through them four times around the circle; Walsh has 64; Fold is continuous. |
| B WORD | Integer 0–255, default 0. B while the B input is unpatched. |
| JUMP, BIT | A bus (or gate) input and the bit it watches. Each publication with that bit high steps SHAPE by one, exactly as on Lookup: the BUS TRIG rule, arming on a fresh clock, rising edges on an unclocked bus. |
| RESET (button and input) | Returns JUMP's steps to zero; wins over a step on the same sample. |
| A | Byte bus: 8 data channels, or 9 with CLK. Its CLK is the output clock. Unpatched, A is 00. |
| B | Byte bus. Patched, it replaces B WORD, read as raw lines on A's sample instants. |
| BUS OUT | The result: 9 channels with A's CLK when A is clocked, 8 otherwise. |
| AUDIO | The result as a raw DAC, `(word − 128) × 5/128` V, like Lookup's. |

Hex display and eight LEDs show the result; the light by JUMP flashes on each
step and the context menu shows the step count.

## Families

**Logic** (shapes mod 16), bitwise on the eight lines:

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| A | A AND B | A OR B | A XOR B | B | NAND | NOR | XNOR |

| 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|
| A AND NOT B | NOT A AND B | A OR NOT B | NOT A OR B | NOT A | NOT B | 00 | FF |

These are all sixteen two-input functions, ordered so that SHAPE 0 passes A.

**Arithmetic** (shapes mod 16), on words 0–255:

| Shape | Result | Shape | Result |
|---|---|---|---|
| 0 | `(A + B) mod 256` | 8 | `min(A, B)` |
| 1 | Mix: `A + B − 128`, clipped (the bipolar sum) | 9 | `A > B` ? FF : 00 |
| 2 | `(A − B) mod 256` | 10 | `A = B` ? FF : 00 |
| 3 | `|A − B|` | 11 | `A + B`, saturating at FF |
| 4 | Product of levels: `A · B / 255`, rounded | 12 | `A − B`, floored at 00 |
| 5 | Ring: `128 + (A − 128)(B − 128)/128`, half up, clipped | 13 | `A · B mod 256` |
| 6 | Mean: `(A + B + 1) / 2` | 14 | `A · B / 256`, the high byte |
| 7 | `max(A, B)` | 15 | `A mod (B + 1)` |

**Fold** (continuous): with `x`, `y` the bipolar values of A and B and
`t = 2π · SHAPE / 64`,

```
u   = drive · (x + sin(t/2) · y) / (1 + sin(t/2)) + 0.75 · sin(t) · x · y
out = 128 + 128 · tri(u), rounded, at most 255
```

where `drive = 1 + 1.5 (1 − cos t)` and `tri` is the triangle fold, the identity
on [−1, 1] reflected beyond. At SHAPE 0 the output is A exactly. Towards 32, B
mixes in, the drive rises to 4 and a product term appears; all of it returns by
64. Small SHAPE changes make small output changes.

**Walsh** (64 shapes): output bit *k* is the parity of
`(A | B << 8) AND mask[k]`. Shape 0's masks pair bit *k* of A with bit *k* of
B, so shape 0 is `A XOR B`; each later shape applies two more reversible row
operations (an XOR of one mask into another, sometimes a swap) chosen by an
original integer hash. Row operations keep the A half and the B half of the
masks each of full rank, so every shape is linear over GF(2) and one-to-one in A
for every B, and in B for every A.

Logic, Arithmetic and Walsh select a discrete shape with Lookup's hysteresis:
half a shape plus 0.05. Fold has no selection; SHAPE is continuous.

## Clocking

As Lookup's Map/Follow: with a clocked A, the result is evaluated on each A CLK
rising edge from A's word and B's raw lines on that sample, and held between
edges, including while SHAPE, B WORD or B change. BUS OUT carries A's CLK level
unchanged. With an unclocked or unpatched A the result is evaluated every sample
and BUS OUT has eight channels. B's own clock is ignored. JUMP is decoded before
A on each sample, so a step and an A edge together evaluate with the new shape.

## Saving and reset

Rack saves the controls and JUMP's steps (`"jump"`). Initialize restores the
default controls and zero steps.

## First patch

1. A **Counter** (MOD 32, INC 3) into A and another (INC 11) into B, both on one
   clock. FAMILY Logic, SHAPE 3: BUS OUT is their XOR on A's clock.
2. Counter 1's CARRY into JUMP: every wrap moves to the next Logic function.
3. FAMILY Fold, AUDIO to a mixer, Counters at audio rates (Engine rate, or the
   internal clock high): SHAPE morphs the terrain they trace.

## Validation

Portable tests (`tests/lookup_test.cpp`): all 65,536 (A, B) pairs through every
Logic function against independent truth tables (all sixteen distinct, four
passes around SHAPE) and every Arithmetic shape against integer and floating
references; every Walsh shape linear over GF(2), one-to-one in A for each B and
in B for each A, all 64 distinct, shape 0 equal to XOR; Fold exactly A at 0 and
64, bounded change for small SHAPE steps, and dependent on B at 32; discrete
selection with hysteresis; A-clocked evaluation and holds; family clamping.

Rack API tests (`tests/lookup_module_test.cpp`, `tests/module_test.cpp`): real
ports; A passing by default; B WORD and B; FAMILY CV and SHAPE CV; a gate on
JUMP walking the Logic functions on a clocked A, with holds; steps saved and
restored; RESET and Initialize; two Counters into A and B with CARRY → JUMP
through one-sample cables in random order, every result equal to the function of
the words on A's edge, identical at 44.1/48/96 kHz.
