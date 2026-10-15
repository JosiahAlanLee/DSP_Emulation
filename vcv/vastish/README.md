# Vastish

An eight-voice synthesizer for VCV Rack 2, with six DSP routing algorithms, fourteen processing functions, sample playback, three envelopes, two LFOs and four FUN modulation processors. Four AUX CV inputs bring external modulation into each voice. Envelope, LFO and FUN outputs can drive other Rack modules.

Developed from this repository's [Pure Data Vastish](https://github.com/JosiahAlanLee/DSP_Emulation/tree/main/pd/vastish) project. Pd is not needed to run the Rack plugin. The instrument uses original programs and no Kurzweil firmware or sample ROM.

- **[User manual](docs/MANUAL.md)** — first sound, controls, patching, saving and troubleshooting.
- **[Installation and getting started](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md)** — all three modules and their sample-sharing chain.
- **[Development and builds](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md)** — SDK setup, tests and source layout.
- **[Validation](docs/VALIDATION.md)** — reference comparisons and implementation limits.

## First patch

Add **VastishLab → Vastish** (56 HP). Patch a pitch/gate source to V/OCT and GATE, and L/R to your mixer or Audio module. The initial program is Ladder Bass; click its name to select another starting program. The sample and all parameter edits save in the Rack patch.

Vastish directly to the right of OTIS can follow its completed sample. Enable **Follow OTIS on the left** in Vastish's right-click menu. This shares stored audio; OTIS's playback settings remain separate.

## Build

From the repository root, with the Rack SDK installed separately:

~~~sh
make -C vcv/vastish RACK_DIR=/absolute/path/to/Rack-SDK -j2
make -C vcv/vastish dist RACK_DIR=/absolute/path/to/Rack-SDK
~~~

The package is written to dist/ inside this plugin directory. Keep the repository's vcv/shared directory when building from source.

Linux live use has been reported. Windows and macOS builds are produced by CI; live use on those platforms remains unverified. Portable OTIS and Spoon Bender engine tests also run on Windows and Apple Silicon.

## License

BSD-3-Clause. Includes DSP adaptations from Pure Data by Miller Puckette and contributors. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
