# DrumLab

**Percussion** is a 20 HP VCV Rack 2 voice with six original digital percussion
models. Each trigger captures its controls. The default four-lane kit connects
to Gorgon's existing polyphonic gates, pitch and velocity.

[Manual](docs/MANUAL.md) · [Gorgon patch](examples/Gorgon-percussion.vcv) ·
[Development](../../docs/DEVELOPMENT.md) ·
[Builds and audition](https://github.com/JosiahAlanLee/DSP_Emulation/actions/workflows/drumlab.yml)

This first prototype provides synthesis. Kit-sample playback, sequenced capture,
Gorgon sound-lock editing and a pattern language are later work. It is an
independent instrument, with no manufacturer firmware, algorithms or samples.

Build separately against the Rack SDK:

```sh
make -C vcv/drumlab RACK_DIR=/absolute/path/to/Rack-SDK -j2
make -C vcv/drumlab/tests check RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/drumlab dist RACK_DIR=/absolute/path/to/Rack-SDK
```

The **DrumLab native Linux** workflow builds the prototype and an original-sound
audition as downloadable Actions artifacts. DrumLab is outside the release
bundles until it has been auditioned in a live patch. Windows/macOS builds are
included in the platform workflow, with portable engine tests on native runners.

To render the 34-second audition locally:

```sh
make -C vcv/drumlab/tests render
vcv/drumlab/tests/render /tmp/DrumLab-audition.wav
```

The first 12 seconds audition the six models in order, 12–20 seconds play a
fixed kit pattern, and 20–32 seconds add per-hit tuning, contour and degradation.
The last two seconds let tails end. This renderer uses the same portable engine
as the Rack module, not a recording of Rack or Gorgon.
