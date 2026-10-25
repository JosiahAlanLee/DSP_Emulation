# Lookup

**ByteBusLab 2.1 · 16 HP · original byte transforms and table authoring**

Lookup maps an 8-bit address to an 8-bit result. It ships six original families,
each with 64 shapes of 256 bytes, and accepts a custom table compiled from a
memoryless Vastish patch. Numeric families morph; Walsh and rhythm families
select discrete shapes with hysteresis. A raw DAC output makes the result
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

The complete generator is [`tools/generate_lookup.py`](../tools/generate_lookup.py).
It uses the Python standard library and no external table data. The shipped
asset `res/lookup-v1.bin` is 98,316 bytes: a 12-byte header plus 96 KiB of table
data. `res/lookup-v1.json` records the generator version, each table's SHA-256,
the Walsh masks and the measured audio statistics. The generated
`LibraryIdentity.hpp` pins the runtime fingerprint. Builtins are loaded and
verified during construction, never generated in `process()`.

The functional role reference is the public [Xaoc Jena manual, rev.
1989/XJ/1.3](https://xaocdevices.com/manuals/xaoc_jena_manual_A4.pdf): byte lookup,
phase modulation, audio/CV transforms, Walsh functions, rhythm generation and
ramp following. The formulas, coefficients, table selection, pattern rules,
clocking and ramp follower here are independently specified. No manufacturer
tables, firmware, chart traces or hardware captures are generator inputs.

Library v1 is immutable: future content changes need a new library identity.
Patches save its SHA-256. A patch asking for a different identity produces a
neutral builtin output and a context-menu diagnostic; embedded custom tables
remain available. The runtime checks the shipped bytes against a versioned
FNV-1a fingerprint; SHA-256 is checked by the generator/asset tests.

## Controls

| Control | Behavior |
|---|---|
| FAMILY | Harmonic, Formant, Fold, Curve, Walsh, Rhythm, Custom. FAMILY CV adds one family per volt and clamps to the ends. |
| SHAPE | Circular 0–64; 64 returns to 0. SHAPE CV adds 64 positions per 10 V. |
| PHASE | ±1 cycle, added to the address modulo 256. PHASE CV adds one cycle per volt, including negative modulation. |
| PATTERN SEED | Integer 0–255, used by Walsh and Rhythm only. Its effect depends only on the current settings/address, not a running random state. |
| MODE | Map, Track rising ramp, Track falling ramp. |
| RESET TRACK / RESET | Button or rising trigger resets the ramp follower; it does not change tables or reset an upstream counter. |
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

OTIS 2.7 can also [export its selected stored frame](../../otislab/docs/BYTE_BUS.md)
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
SHA-256 for provenance.

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
at 44.1/48/96 kHz. The audio path performs no heap allocations. GUI and physical
device behavior require separate workstation checks.
