# Lookup

**ByteBusLab 2.11 · 16 HP · original byte transforms and table authoring**

Lookup maps an 8-bit address to an 8-bit result. It ships ten original families,
each with 64 shapes of 256 bytes, and accepts a custom table compiled from a
memoryless Vastish patch. Numeric families morph; Walsh, Rhythm, Density and
Ratio select discrete shapes with hysteresis. A raw DAC output makes the result
audible without another converter.

## First sound

1. Patch a VCO saw wave at approximately ±5 V into **SIGNAL**, and **AUDIO**
   to a mixer. Leave **BUS IN** unconnected; a connected bus takes priority.
2. Leave **MODE = Map**, **FAMILY = Harmonic**, PHASE at 0, and turn SHAPE.
3. Try Formant and Fold. FAMILY, SHAPE and PHASE can be modulated through their
   corresponding CV inputs.
4. For an imperfect saw ramp, try **Track rising ramp**. The LOCK light comes
   on after two consistent measured periods. Use Track falling for a falling ramp.

SIGNAL is an ADC address input: −5 V → 00, 0 V → 80, +5 V → FF, with saturation
outside that range. It reads the first channel. A connected BUS IN owns the
address and clock even when its clock is stopped. With neither input connected,
BUS OUT is neutral 80 and AUDIO is 0 V.

## Families and provenance

| Family | Original construction | SHAPE and SEED |
|---|---|---|
| Harmonic | A fundamental and independently specified 2nd, 3rd and 5th partials; their weights/phases follow a circular parameter path. | Numeric morph, including 63 → 0; SEED unused. |
| Formant | Three moving Gaussian peaks in harmonic-number space over 32 partials, with an original phase arrangement. These are generated spectra, not sampled voices. | Numeric circular morph; SEED unused. |
| Fold | A sine through an analytic triangle fold, with circular drive and bias controls. | Numeric circular morph; SEED unused. |
| Curve | Identity, power, smoothstep, square-root and inverted anchors. Shape 0 is exact identity; 32 is exact byte inversion, 255−n. | Numeric circular morph; SEED unused. |
| Walsh | Parity functions from eight independent binary masks, constructed by reversible row swaps and XOR additions. Every shape and seed is a byte permutation. | Discrete shape; SEED XORs the address, changing bit-plane polarities. |
| Rhythm | Original 32-step, eight-lane patterns with fixed kick/snare anchors, arithmetic pulse distribution and rotations. | Discrete shape; nonzero SEED toggles one step per lane 2–7; kick/snare anchors persist. |
| Custom | A verified 256-byte table imported from JSON, embedded when the patch is saved. | Shape 0 = identity, 32 = imported map; other shapes interpolate around that circle. SEED unused. |
| Envelope | Level over time, for a One-shot Counter's ramp: exponential decays from linear to sharp (0–7), logarithmic decays (8–15), attack then decay with a 2–45% attack (16–23), rises from exponential through linear to logarithmic that end at the top (24–31), full level held 10–80% then a cliff (32–39), two-stage cliffs through a half-level plateau (40–47), 2–5 falling lumps (48–55), and attack-decay-sustain with sustain 0–88% (56–63). The decays end at 00 and the sustains at their level, which a held One-shot keeps. | Numeric circular morph; SEED unused. |
| Sequence | Stepped CV from a counter's phase: staircases up with 2–16 steps and down with 16–2 (0–15), ridges up and down over 3–16 steps, centred and skewed (16–31), staircases and ridges whose risers fall on Euclidean positions E(3–10, 16) (32–47), and a ball bouncing over 16 or 32 steps, keeping 45–80% of its height per bounce (48–63). | Numeric circular morph; SEED unused. |
| Density | Rhythm's layout with nested patterns: shape 0 is kick on 0 and 16 and snare on 8 and 24, and each shape adds exactly one hit to the one before, in a fixed order ranked by lane and metric position: closed hat on the beats, then on the 8ths, then kick, open hat, percussion and snare ghosts on beats and 8ths. No shape uses a 16th-note position. Shape 63 has 67 hits. SHAPE is a fill control: more is always a superset. | Discrete shape; SEED unused. 63 wraps to 0, the sparsest. |
| Ratio | A control byte to a *ratio word* for Counter 2's RATIO input: n − 1 in the high four bits, d − 1 in the low four, so 1/16 to 16. Eight grids of exact ratios, all containing 1/1: Integers (1/16 … 16), Odds (1/15 … 15), Threes (1/15 … 15 by threes), Harmonics (every n/d with n, d ≤ 4), Straight (powers of two), Triplet (straight and ×3/2), Dotted (straight and ×2/3), and All. Shape = 8 × grid + span; the spans reach 1, 2, 3, 4, 6, 8 or 12 ratios either side of 1/1, or the whole grid. The addresses split into equal zones, slowest ratio at 00. | Discrete shape; whole addresses only, so a word is never blended; SEED unused. |

The complete generator is [`tools/generate_lookup.py`](../tools/generate_lookup.py).
It uses the Python standard library and no external table data. The shipped
asset `res/lookup-v1.bin` is 98,316 bytes: a 12-byte header plus 96 KiB of table
data. `res/lookup-v1.json` records the generator version, each table's SHA-256,
the Walsh masks and the measured audio statistics. The generated
`LibraryIdentity.hpp` pins the runtime fingerprint. Builtins are loaded and
verified during construction, never generated in `process()`.

Envelope, Sequence and Density are library v2, `res/lookup-v2.bin`: 49,164
bytes, a 12-byte `BBLOOK02` header plus 48 KiB, from the same generator, with
`res/lookup-v2.json` recording each table's SHA-256, the Density hit counts and
the order Density adds hits in. They are FAMILY 7, 8 and 9, after Custom, so a
FAMILY saved by an earlier version still names the same family. Without v2 they
are neutral (80) and the v1 families are unaffected.

Ratio is library v3, `res/lookup-v3.bin`: 16,396 bytes, a `BBLOOK03` header
plus 16 KiB, with `res/lookup-v3.json` listing the grids as exact fractions and
each shape's ratios. It is FAMILY 10. The grids are named after the Via SYNC's
ratio grid categories (integers, odds, multiples of three, rhythmic harmonics,
no-triplet, triplet, dotted); their contents here are original.

The knob's saved family is kept, but the FAMILY CV range is not: FAMILY + CV
clamps to 0–10 (0–6 before 2.7, 0–9 before 2.11). In a patch from an earlier
version, a FAMILY + CV sum above that version's top now reaches the newer
families: a sum from 6.5 up plays Envelope, Sequence, Density or Ratio where a
patch from before 2.7 stayed on Custom, and from 9.5 up it plays Ratio where a
patch from before 2.11 stayed on Density.

The functional role reference is the public [Xaoc Jena manual, rev.
1989/XJ/1.3](https://xaocdevices.com/manuals/xaoc_jena_manual_A4.pdf): byte lookup,
phase modulation, audio/CV transforms, Walsh functions, rhythm generation and
ramp following. The formulas, coefficients, table selection, pattern rules,
clocking and ramp follower here are independently specified. No manufacturer
tables, firmware, chart traces or hardware captures are generator inputs.

Library v1 is immutable: future content changes need a new library identity,
as v2 and v3 are. Patches save all three SHA-256s. A patch asking for a different identity
produces a neutral output from that library's families only, with a
context-menu diagnostic; embedded custom tables remain available. A patch from
before 2.7 names no v2 library (before 2.11, no v3) and plays those families
normally if switched to them. The runtime checks the shipped bytes against a versioned
FNV-1a fingerprint; SHA-256 is checked by the generator/asset tests.

## Controls

| Control | Behavior |
|---|---|
| FAMILY | Harmonic, Formant, Fold, Curve, Walsh, Rhythm, Custom, Envelope, Sequence, Density, Ratio. FAMILY CV adds one family per volt and clamps to the ends, 0 and 10. |
| SHAPE | Circular 0–64; 64 returns to 0. SHAPE CV adds 64 positions per 10 V, and JUMP adds its steps. |
| PHASE | ±1 cycle, added to the address modulo 256. PHASE CV adds one cycle per volt, including negative modulation. |
| PATTERN SEED | Integer 0–255, used by Walsh and Rhythm only. Its effect depends only on the current settings/address, not a running random state. |
| MODE | Map, Track rising ramp, Track falling ramp. |
| RESET TRACK / RESET | Button or rising trigger resets the ramp follower and returns JUMP's steps to zero; it does not change tables or reset an upstream counter. |
| JUMP, BIT | A bus (or gate) input and the bit it watches, 0–7. Each publication with that bit high steps SHAPE by one; see [JUMP](#jump). |
| Map clock policy | Context menu: Follow bus CLK (default), or Engine rate. Saved as a parameter. Track modes always generate an unclocked output at the engine rate. |

Numeric families interpolate both between table addresses and neighboring
shapes, then round to the nearest byte. At a discontinuity such as the Curve
identity ramp's 255 → 0 boundary, fractional address interpolation crosses that
edge; it does not extrapolate the ramp. Walsh and Rhythm use integer addresses
and discrete shape selection. The selection threshold is half a shape plus
0.05 of hysteresis. This avoids inventing intermediate gate masks by numeric
interpolation.

Rhythm stores each hit high for the first four of its eight phase addresses,
then low for four. Consecutive hits therefore have a low gap if the phase source
visits those addresses. Skipping across a pulse window can miss it. Use a phase
source slow enough for the desired gates; input phase is not an event queue.

## JUMP

JUMP steps SHAPE rhythmically from logic: each step adds one shape position,
wrapping at 64, and the steps stay until RESET. They add to the knob and SHAPE
CV, so a step from SHAPE 5 reads shape 6 of a discrete family and morphs a
numeric one by one table; on Custom, one step is 1/64 of the morph's cycle.

When a step happens is the Machinemodule
[BUS TRIG](../../machinemodule/docs/BUS_TRIG.md) rule, with BIT choosing the line:

- **Clocked JUMP (nine channels):** each CLK rising edge where the bit is high
  steps once. Adjacent high words step twice, and a bit held high across
  publications steps on each one. The first edge after the cable is connected,
  or after it gains its clock, only arms: a Reader counts a CLK that is already
  high as an edge, and that must not replay a held bit.
- **Unclocked JUMP (eight or fewer channels):** each rising edge of the bit
  steps. A bit already high when the cable arrives, when it loses its clock, or
  when BIT moves onto it does not. A gate or trigger cable is bit 0.

JUMP is decoded before the address on the same sample, so a step and an address
arriving together (one source patched to both) evaluate with the new shape. In
Map/Follow the output still changes only on BUS IN's edges; a step between them
shows at the next one. RESET clears the steps and wins over a step on the same
sample. The steps are saved with the patch; Initialize clears them, and a patch
from before 2.5 has none. The light beside JUMP flashes on each step, and the
context menu shows the count.

A held offset ("+1 while high") is not JUMP: that is attenuated SHAPE CV, one
shape per 10/64 V (about 0.156 V). A 10 V bit straight into SHAPE CV is a full
turn and lands where it started.

A Gorgon v3 BUS OUT lane bit into JUMP steps on every publication its hit
covers, as BUS TRIG does: one step per hit only when Gorgon's CLOCK pulses once
per row and the lane has no `ply`.

## Bus timing and tracking

In Map/Follow, a nine-channel bus is sampled and transformed on its CLK rising
edge. The output has nine channels, with the same clock level, and holds its
word between edges, including while CV changes. A shorter input emits an
eight-channel unclocked bus. Map/Engine samples the current decoded lines and
controls every engine sample and emits eight channels. All bits are exact
0/10 V, LSB first. Short cables do not broadcast to missing channels.

Track is an explicit stateful mode. It observes the input's sampling events,
detects directional ramp returns larger than 64 byte codes, measures periods,
and generates its own fractional forward phase at the engine rate. It requires
at least two forward movements in a cycle; backward jumps larger than four
codes invalidate the cycle. Two measured periods within 25% establish lock.
Accepted periods span four engine samples to 120 seconds. The current period
estimate is the mean of the last two consistent periods.

While unlocked, the input byte is mapped directly. After 2.5 estimated periods
without a return the follower loses lock and falls back to mapping. RESET
clears the estimates. A rising/falling mode change also clears them. Track
always emits eight unclocked channels; a stopped upstream clock does not freeze
its prediction immediately. Choose Map/Follow for exact clocked holds.

AUDIO is exactly `(word−128)×5/128` V, with no output filter or limiter.
It is 8-bit audio at Rack's engine rate, including quantization and possible
aliasing. The input/output bus is a Rack convention, not a hardware-compatible
MHz-rate interface. Display/driver/hardware timing is outside the software tests.

## Compile a Vastish table

Vastish 2.5 BUS I/O also applies its live DSP chain to byte streams, including
stateful filters and delays. The table compiler remains a frozen memoryless
snapshot. Compiler v2 supports the new sources and per-FUN BIT flags, and
accepts historical 80-parameter/18-source snapshots by padding the appended
fields. Existing compiled v1 Lookup tables remain unchanged and importable.

OTIS 2.8 can also [export its selected stored frame](../../otislab/docs/BYTE_BUS.md)
in the same custom-table format. Left, right or mono-mix export lets recordings
and original REWORK results become Lookup material; load the exported JSON with
the existing table menu. A later OTIS edit requires another export.

The compiler evaluates the existing Vastish F1–F3 code at every possible byte
input. It supports all six routings and **NONE, SHAPER, DIST, GAIN and CRUSH**.
Every block must be memoryless, including blocks unused by the selected routing.
The source oscillator/sample is replaced by the byte address. The PITCH column
sets phase rotation: 12 semitones is one cycle. The result is before the amp
envelope, master, pan and output DC blocker.

Build the standalone backend; authoring needs C++17 and Python 3.10+, without
the Rack SDK:

```sh
make -C vcv/bytebus/tools
python3 vcv/bytebus/tools/compile_vastish.py my-patch.vcv \
  --module-id 17 --note 60 --velocity 1 --output my-table.lookup.json
```

The wrapper accepts plain Rack JSON, ZIP, and zstd-compressed JSON/tar patches.
The last form requires the `zstd` command. It selects the sole Vastish instance,
or the explicit module ID when there are several. It also accepts a Vastish
module preset or `ByteBusLab.VastishRecipe/1` recipe. Input files are read only.
Rejected stateful patches leave any existing output file unchanged.

A saved patch contains parameter settings, not the live modulation state.
Default frozen values are note 60, velocity 1, and zero for other sources.
Recipes can supply a complete `frozen.sources` array. OFF is forced to zero,
KEYNUM/VEL are derived from note/velocity, and FUN1–4 are recomputed from those
frozen inputs with Vastish's existing cycle handling. The output records the
full parameters, frozen inputs, compiler version and hashes.

Reproduce the included example:

```sh
python3 vcv/bytebus/tools/compile_vastish.py \
  vcv/bytebus/examples/vastish-sine.recipe.json \
  --name 'Vastish SHAPER 2' --output my-sine.lookup.json
```

In Lookup's context menu choose **Load original Lookup table (.json)**.
Successful loading selects Custom with SHAPE 32. The file must declare
`ByteBusLab.LookupTable`, version 1, exactly 256 integer `values` in 0–255,
and a matching `fnv1a64:…` hash. Name and source recipe are retained, and the
complete table is embedded in saved patches. Reload does not need the source
file. The import validates that fingerprint; the compiler additionally records
SHA-256 for provenance. A patch whose embedded table fails these checks (a
hand-edited value, say) loads with Custom as identity and shows the reason in
the context menu; saving it then embeds the identity table.

The compiler uses the repository's existing Vastish implementation, including
its acknowledged Pure Data-derived DSP equations. See
[Vastish third-party notices](../../vastish/THIRD_PARTY_NOTICES.md). The six
builtin table families are generated independently by the formulas above.

## Feedback and validation

For a short test, Register BUS OUT → Lookup BUS IN → Register BUS IN. Select
Curve, SHAPE 32, and give the register an external or Internal clock. With
reset word A5, its words alternate A5, 5A, A5 after LOAD. The loop must settle
before the next acquisition edge; two module hops cost two engine samples.

Tests exhaust all byte inputs for every Walsh shape/seed, verify rhythm gaps
and anchors, bound DC/RMS and every shape seam, compare the compiler to the
actual Vastish Voice implementation, and exercise real Rack ports, import,
patch restore, UI/audio handoff, packaged assets, tracking and reset feedback
at 44.1/48/96 kHz. JUMP is tested through a real Reader (arming, held and
adjacent high bits, gates, Schmitt thresholds, clocked/unclocked changes,
disconnection, wrap) and through the module (a step on its own sample, bit
choice, RESET priority, persistence, Initialize), and a Counter feeding both
BUS IN and JUMP through one-sample cables in random order gives the expected
Walsh shape for every count at 44.1/48/96 kHz. Library v2 tests check its
identity and three shapes of every family against the engine; the envelope
endings, staircase levels, Rhythm layout and one-hit nesting of every Density
shape; morphs and discrete selection; per-library identities in saved patches;
One-shot Counter → Envelope restarting on each gate; and Counter CARRY → JUMP
on Density adding one hit per bar for twelve bars. Library v3 tests check its
identity, that every byte is a ratio word of its grid, ascending in equal zones
around 1/1 over the shape's span, that the engine never blends two words, even
at a fractional address, and that Lookup Ratio into Counter 2's RATIO gives the
chosen ratio's carries through one-sample cables in random order. The audio
path performs no heap allocations. GUI and physical device behavior require
separate workstation checks.

## First sample after Initialize or restore

Fresh instances, Initialize and restore reset the held Map/Follow result
to neutral 80. With clocked BUS IN initially low, that result holds until
the first rising CLK; initially high CLK latches and maps the word immediately.
This follows the bus input clock, rather than generating an acquisition strobe.
Unclocked BUS IN, SIGNAL, Map/Engine and Track evaluate immediately (Track
starts unlocked). A connected BUS IN takes priority over SIGNAL.
JUMP already high does not step: an unclocked bit is primed at its current
level, and the first clocked publication only arms JUMP. Restored JUMP steps
are kept unless RESET is already high, which clears them once and resets
the follower. Held RESET allows later JUMP edges. Restore rearms RESET,
including when loading into a running instance.
