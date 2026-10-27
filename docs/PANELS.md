# VCV panel guide

The newer instruments follow the same panel language as OTIS, Vastish, Spoon
Bender and Séance: dark blue-grey panels, mint titles and outputs, amber section
labels, pale control labels, and darker blocks around outputs. The twelve
[panel marks](../vcv/shared/glyphs/README.md) identify the modules.

![Register, Lookup, Converter, DrumLab and Machinemodule in Rack](images/new-vcv-panels.png)

These are actual module-browser renders from Rack Free 2.6.4 on Linux. The
browser illuminates indicators for its previews; the lights above do not show a
running patch. Machinemodule requires a user-supplied OS to make sound.

| Panel | Width | GUI details | Generated layout illustration |
|---|---|---|---|
| CZish | 30 HP | Line and waveform selectors, six eight-step envelope editor, endpoint selectors, live word/divider display, grouped mono/poly/bus outputs | [CZish](../vcv/czish/docs/Czish-preview.svg) |
| DAC Stage | 16 HP | Converter selector, rate/bits and bus readouts, noise/tone controls, bit CV, grouped audio/bus outputs | [DAC Stage](../vcv/czish/docs/DacStage-preview.svg) |
| Clocked Register | 12 HP | Named clock source selector, held-word hex display, separate bus and clock outputs | [Register](../vcv/bytebus/docs/Register-preview.svg) |
| Lookup | 16 HP | Named family and Map/Rise/Fall selectors, JUMP jack with bit selector, output hex display, grouped bus/audio outputs | [Lookup](../vcv/bytebus/docs/Lookup-preview.svg) |
| Lookup2 | 16 HP | Named family selector, SHAPE and B WORD knobs, JUMP jack with bit selector and reset, output hex display, A/B/CV/RESET inputs, grouped bus/audio outputs | [Lookup2](../vcv/bytebus/docs/Lookup2-preview.svg) |
| Counter | 12 HP | Named clock source and run mode selectors, RATE/INC/MOD/SEED knobs, count hex display, grouped bus, clock and carry outputs | [Counter](../vcv/bytebus/docs/Counter-preview.svg) |
| Switch | 12 HP | Named clock source, mode and select-bit selectors, RATE/B WORD/C WORD knobs, result hex display, A/B/C/CLOCK inputs, grouped bus and clock outputs | [Switch](../vcv/bytebus/docs/Switch-preview.svg) |
| Pipeline | 12 HP | Named clock source selector, RATE/DELAY/SPREAD knobs, LOOP latch and CLEAR buttons, output hex display, BUS/CLOCK/RESET/LOOP inputs, grouped bus and clock outputs | [Pipeline](../vcv/bytebus/docs/Pipeline-preview.svg) |
| Counter 2 | 12 HP | Named clock source selector, RATE/N/D/SEED knobs, hex and N/D readouts, CLOCK/RESET/RATIO inputs, grouped bus, clock and carry outputs | [Counter 2](../vcv/bytebus/docs/Counter2-preview.svg) |
| Converter | 14 HP | Independent ADC/DAC readouts, gain/offset/range controls, grouped outputs | [Converter](../vcv/bytebus/docs/Converter-preview.svg) |
| Fan | 16 HP | Five address LEDs and step readout, SOURCE/DIR/UNSELECTED selectors, GLITCH latch, RATE/LENGTH/SEED knobs, CLOCK/RESET/DIR/BUS and COM/IN 1-16/IN 17-32 inputs, grouped bus, clock, EOC, COM and 16-channel outputs | [Fan](../vcv/bytebus/docs/Fan-preview.svg) |
| Lattice | 26 HP | 4 x 4 step knobs with step lights, V/H hex readout, JUMP/SOURCE/CV RANGE selectors, RATE/SEED/SLEW knobs, eight clock/direction/jump/reset/bus inputs and two slew CVs, grouped CV, slew, bus, clock and gate outputs | [Lattice](../vcv/bytebus/docs/Lattice-preview.svg) |
| Parallel Sum | 20 HP | Word and mask hex readouts, SOURCE/SENSE selectors, RATE/MASK/THRESH/SEED knobs, HOT latch, a row of eight stage weights with stage lights, CLOCK/RESET/INJECT/MASK/W CV inputs, grouped bus, clock, gate, CV and audio outputs | [Parallel Sum](../vcv/bytebus/docs/ParallelSum-preview.svg) |
| Wheel | 14 HP | LOOP hex readout and bit lights, SOURCE/STRIDE selectors, LOAD and CAPTURE buttons, RATE/LENGTH/OFFSET/SEED knobs, CLOCK/RESET/CAPTURE inputs, grouped LOOP/FREE bus, clock, U, G and gate outputs | [Wheel](../vcv/bytebus/docs/Wheel-preview.svg) |
| Roulette | 14 HP | VCO frequency readout, twelve count lights, SOURCE/COMPARATOR/SCALE selectors, RANGE/FILTER knobs, TRACK/ROULETTE/DIVIDE/RANGE CV/RESET inputs, grouped LO/HI/RESULT bus, VCO and clock outputs | [Roulette](../vcv/bytebus/docs/Roulette-preview.svg) |
| Otis Kit | 18 HP | Eight slot rows of TUNE/DECAY/LEVEL/FILTER/START knobs with REV switches and hit lights, GATE/SLOT/PITCH/LEVEL inputs, grouped output | [Otis Kit](../vcv/otislab/docs/OtisKit-preview.svg) |
| Otis Capture | 8 HP | Status display, named slot, length and source selectors, GAIN/TEMPO knobs, ARM button, IN/CLOCK/ARM inputs, grouped REC/DONE outputs | [Otis Capture](../vcv/otislab/docs/OtisCapture-preview.svg) |
| Machinemodule | 18 HP | Local machine ID/name readout, labelled Ready/Link indicators, eight offset/CV pairs, grouped outputs | [Machinemodule](../vcv/machinemodule/docs/Machinemodule-preview.svg) |

The machine readout describes the local selector. A patched machine CV or an
incoming Gorgon sound lock can select a different machine per hit. P1–P8 remain
raw offsets; machine-specific parameter names are available in the existing
module menu. The readout gets names from the loaded OS.

The earlier panel update preserves its parameter, port and light IDs, patch data
and module widths. CZishLab 2.1 separately expands CZish from 16 to 30 HP; its
existing IDs and cables are preserved and new controls are appended. Existing patches keep their connections and values. Moving the
controls changes where their cables are drawn.

## Regenerate artwork

Use a complete repository checkout, Python 3, `fontTools` and DejaVu Sans.
The generated SVGs contain outlined labels: NanoSVG, Rack's panel renderer,
does not draw SVG `<text>`. DejaVu's license accompanies each affected plugin.
No font installation or Python is needed to use the built plugins.

From the repository root:

```sh
python vcv/bytebus/tools/make_panels.py
python vcv/machinemodule/tools/make_panel.py
python vcv/seance/tools/make_panel.py
python vcv/czish/tools/make_panels.py
```

These use [the shared panel helper](../vcv/shared/panels/panel.py).
Each component table generates both the artwork and `src/Layout.hpp`, consumed
by the module widget, plus a component illustration in the plugin's `docs`
directory. Commit all generated outputs together. The old
`make_converter_panel.py` entry point regenerates the whole ByteBusLab family.
An alternate font file can be passed to these generators with `--font`.
The component illustrations are layout aids, not Rack screenshots.

Keep layout namespace imports inside widget constructors. Qualify shared names
such as `WIDTH` so integration tests can compile several adapters together.

Séance already follows this style; its narrow Cross switch legends now sit
inside the 4 HP panel border. The established OTIS, Vastish and Spoon Bender
generators remain in their respective `tools` directories.

## Validation for this update

Linux builds for ByteBusLab, DrumLab and Machinemodule pass. All six ByteBusLab
engine/module checks, DrumLab's engine/module/Gorgon checks, and Machinemodule's
six firmware-free CTest checks pass. Séance's engine/module/Bridge checks also
pass, including the native OTIS/Lookup feedback integration and ASan/UBSan
checks. Local sanitizer runs disable leak detection because this host cannot
inspect process threads. Rack's own screenshot mode loads and draws
all five updated module widgets and all six Séance panels, including browser
previews without a module instance. Live pointer/keyboard interaction and
physical audio/CV devices were not part of this check.
