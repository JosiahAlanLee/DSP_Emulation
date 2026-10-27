# ByteBusLab

Twelve modules for the [byte bus](../../docs/BYTE_BUS_SPEC.md), with exact bits,
explicit clocking and original table content:

| Module | Role | Manual |
|---|---|---|
| **Converter**, 14 HP | Independent ADC/DAC paths, +/-5 or +/-10 V ranges, gain/offset trims and explicit clock policies. | [Converter](docs/CONVERTER.md) |
| **Clocked Register**, 12 HP | One word of state, selectable clock and reset word; an explicit sampling point for feedback. | [Register](docs/MANUAL.md) |
| **Lookup**, 16 HP | Ten generated waveform/CV/Walsh/rhythm/envelope/sequence/density/ratio families, numeric morphs, ramp tracking, analog or byte input, and a raw DAC output. JUMP steps SHAPE from a bus bit or a gate. Imports original Vastish tables. | [Lookup](docs/LOOKUP.md) |
| **Lookup2**, 16 HP | Two words in, one out: all sixteen bit-logic functions, byte arithmetic (mix, ring, compare), a two-input fold and two-word Walsh maps, on A's clock, with JUMP. Computed, so no tables. | [Lookup2](docs/LOOKUP2.md) |
| **Switch**, 12 HP | Two words in, one out, under a control word C: Select A or B by a bit of C, take each bit from A or B, or Mix from A to B. Follows A's clock or reclocks on its own. | [Switch](docs/SWITCH.md) |
| **Counter 2**, 12 HP | A phase word at N/D of its clock (N 1–16, D 1–64, or a ratio word on RATIO from Lookup's Ratio family), exact for ever, with CARRY on each cycle: polymeter and cross rhythms locked to a clock. | [Counter 2](docs/COUNTER2.md) |
| **Pipeline**, 12 HP | A word returns DELAY (1–64) edges later; SPREAD delays each bit a little more, and LOOP repeats the last DELAY words. The canon of the kit. | [Pipeline](docs/PIPELINE.md) |
| **Counter**, 12 HP | A word that counts: increment from a knob or a byte bus, modulus 2–256, seed and carry, with Register's clock rules. Loop, One-shot or Pendulum: a divider chain, a ramp that holds, or a triangle. | [Counter](docs/COUNTER.md) |
| **Fan**, 16 HP | One 5-bit address, 32 places: routes COM IN to one of 32 outputs and one of 32 inputs to COM OUT, on 16-channel cables, from an up/down counter of 1–32 steps or a byte bus. Hold, and an optional late-enable GLITCH after the Nonlinearcircuits 32:1. | [Fan](docs/FAN.md) |
| **Lattice**, 26 HP | Sixteen steps on a 4 × 4 grid, column from one up/down counter and row from another on its own clock, with the cross-coupled jumps of the Nonlinearcircuits Bindubba (settled or one-pass) or jumps from a byte bus; CV, inverted CV, two slews under CV and sixteen gates. | [Lattice](docs/LATTICE.md) |
| **Parallel Sum**, 20 HP | An eight-stage shift register, XOR or XNOR feedback from a mask (a knob or a byte bus) plus an injected signal, summed through eight weights into a stepped CV and a zero-mean audio output: a timbre generator, a CV pattern and controllable noise, after the Nonlinearcircuits Burhans Timbre. | [Parallel Sum](docs/PARALLEL_SUM.md) |
| **Wheel**, 14 HP | A 24-stage noise register that visits every 24-bit state once, and a loop of 1–24 steps that recirculates until CAPTURE refills it from the noise; both read through an eight-stage window as byte buses, a DAC and a bell-curve CV, after the Nonlinearcircuits Pseudo. | [Wheel](docs/WHEEL.md) |

Plugin **ByteBusLab**, version **2.15.0**. For a first sound, patch a VCO saw into
Lookup SIGNAL and its AUDIO output to your mixer; turn SHAPE and FAMILY.

Build and check from the repository root:

```sh
make -C vcv/bytebus RACK_DIR=/absolute/path/to/Rack-SDK -j2
make -C vcv/bytebus/tests check RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/bytebus/tests tools-check
make -C vcv/bytebus/tests sanitize RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/bytebus dist RACK_DIR=/absolute/path/to/Rack-SDK
```

Portable tests require no Rack SDK: `make -C vcv/bytebus/tests test`.
Rack tests run Register/Vastish FUN and Register/Lookup feedback with one-sample
cables, random module order and RESET replay at 44.1/48/96 kHz. Lookup tests also
check every Walsh shape/seed, rhythm gates, audio bounds and seams, imports,
tracking, persistence, JUMP steps, and compiler parity with the actual Vastish Voice.
I/O tests also cover calibrated conversions, all 65,536 two-byte XOR pairs,
gate-free Vastish DSP, and the delayed Converter/Register/Vastish/Lookup/OTIS
chain. Counter tests cover every word/increment/modulus in all three run modes,
CARRY chains, a Counter → Lookup rhythm and One-shot → Lookup envelopes. Switch tests cover every (A, B, C) in all three
modes, a Counter A/B commutator and reclocking. Pipeline tests cover every DELAY and
SPREAD, loops, a saved looped phrase and a Rhythm canon through Switch. Lookup2
tests cover every (A, B) in Logic and Arithmetic and every Walsh shape; Counter 2
tests every N/D against the exact formula and a 12-against-16 polymeter; Fan tests every address, length and direction, routing both ways, GLITCH and a Counter → Fan chain. Lattice tests both jump modes against the jam wiring for all 256 states, held and simultaneous jumps, and a Counter → Lattice jump chain. Parallel Sum tests every mask under XOR and XNOR, the original's 729 switch settings, the outputs as a convolution of the bit stream, and a Counter → MASK bus chain. Wheel tests the full 2^24 cycle, every window and loop length, and capture on the beat from a Counter's CARRY. Try `examples/byte-bus-io.vcv` with Vastish 2.5 and OTIS
2.8 installed, and `examples/counter-divider.vcv` with ByteBusLab alone.

The [generator](tools/generate_lookup.py) and per-table manifests ship with the
source; library v1 is unchanged, v2 adds Envelope, Sequence and Density, and v3
adds Ratio.
Regenerate/verify assets with `python3 vcv/bytebus/tools/generate_lookup.py
--check`. The [Lookup manual](docs/LOOKUP.md) covers authoring tables from saved
Vastish patches and includes an original sine recipe and importable example.

Prototype: not in the release bundles or the VCV Library.
Hardware clock paths are outside these software tests.

Converter's outlined panel labels use DejaVu Sans; its
[font license](docs/DEJAVU-LICENSE.txt) accompanies the generated SVG.
