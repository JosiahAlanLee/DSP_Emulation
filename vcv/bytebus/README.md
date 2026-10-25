# ByteBusLab

Two modules for the [byte bus](../../docs/BYTE_BUS_SPEC.md), with exact bits,
explicit clocking and original table content:

| Module | Role | Manual |
|---|---|---|
| **Clocked Register**, 12 HP | One word of state, selectable clock and reset word; an explicit sampling point for feedback. | [Register](docs/MANUAL.md) |
| **Lookup**, 16 HP | Six generated waveform/CV/Walsh/rhythm families, numeric morphs, ramp tracking, analog or byte input, and a raw DAC output. Imports original Vastish tables. | [Lookup](docs/LOOKUP.md) |

Plugin **ByteBusLab**, version **2.1.0**. For a first sound, patch a VCO saw into
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
tracking, persistence, and compiler parity with the actual Vastish Voice.

The [generator](tools/generate_lookup.py) and per-table manifest ship with the
source. Regenerate/verify assets with `python3 vcv/bytebus/tools/generate_lookup.py
--check`. The [Lookup manual](docs/LOOKUP.md) covers authoring tables from saved
Vastish patches and includes an original sine recipe and importable example.

Prototype: not in the three-instrument release bundles or the VCV Library.
Hardware clock paths are outside these software tests.
