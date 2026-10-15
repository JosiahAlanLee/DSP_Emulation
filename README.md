# DSP_Emulation

Three instruments for VCV Rack 2: a sampler, a synthesizer and a stereo effects processor, with optional sample sharing between them. The repository also contains the original Pure Data Vastish project.

**[Installation and getting started](docs/GETTING_STARTED.md)** · **[Development and builds](docs/DEVELOPMENT.md)**

| Path | What | Status |
|---|---|---|
| [`pd/vastish`](pd/vastish) | Kurzweil K2000/K2500 VAST-style 8-voice synth in vanilla Pure Data (0.52+) | working; tested headless |
| [`vcv/vastish`](vcv/vastish) | Native C++ port of Pd Vastish for VCV Rack 2; eight voices, six algorithms, fourteen DSP functions | DSP and Rack API tested; Linux tested live in Rack; Windows/macOS build only |
| [`vcv/otislab`](vcv/otislab) | EPS-16+-style sample player for VCV Rack 2, built on MAME-derived ES5505 (OTIS) playback arithmetic, with experimental window/frame scanning and loop reworking | prototype; engine bit-exact vs reference core; Linux tested live in Rack |
| [`vcv/spoonbender`](vcv/spoonbender) | Echo/space effects: echo with an in-loop frequency shifter, FDN space, optional numerical character at a reduced rate, retroactive capture to OTIS | DSP and Rack API tested headless; Linux tested live in Rack (capture to OTIS) |

## User manuals

- [OTIS Sampler](vcv/otislab/docs/MANUAL.md): record, load, transpose, scan and rework a sample.
- [Vastish](vcv/vastish/docs/MANUAL.md): route three DSP blocks, shape voices and patch modulation.
- [Spoon Bender](vcv/spoonbender/docs/MANUAL.md): echo, frequency shifting, space, freeze and capture.

Each module works independently. For sample sharing, place them directly adjacent in the order **Spoon Bender → OTIS → Vastish**, and enable the receiving modules' follow options. Audio still travels through patch cables.

## Builds

CI builds the Rack plugins for Linux x64, Windows x64 and macOS (arm64 and x64) on
every push; the `.vcvplugin` files are attached to each run under the repository's
Actions tab. Only the Linux builds have been run inside Rack. The Windows and macOS
builds compile, pass OtisLab's and Spoon Bender's portable engine tests (Windows and Apple Silicon), and
link only against Rack and the OS, but nobody has loaded them yet. Reports welcome.

Kurzweil, VAST, Ensoniq and EPS are their owners' trademarks. These are independent emulations, not affiliated with or derived from manufacturer code, firmware or samples. OtisLab's playback arithmetic is adapted from MAME (BSD-3-Clause, Aaron Giles); see its THIRD_PARTY_NOTICES.
