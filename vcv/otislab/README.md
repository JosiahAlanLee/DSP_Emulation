# OTIS Sampler

A mono sampler for VCV Rack 2 with MAME-derived ES5505 playback arithmetic, per-voice chip filters and level control. LAB extensions add variable clocking, interpolation control, short-window scanning, frame stepping and background sample reworking.

OTIS records audio or loads WAV files. It uses no Ensoniq firmware, operating system or commercial sample library. It is an independent sampling instrument, not a complete EPS-16+ recreation.

- **[User manual](docs/MANUAL.md)** — first sound, controls, patching, saving and troubleshooting.
- **[Installation and getting started](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md)** — all three modules and their sample-sharing chain.
- **[Development and builds](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md)** — SDK setup, tests and source layout.
- **[Validation](docs/VALIDATION.md)** — reference comparisons and implementation limits. See also [change history](docs/CHANGELOG.md).

## First patch

Add **OtisLab → OTIS Sampler** (36 HP). Choose **Load included demo** from the right-click menu, patch L to your mixer or Audio module, and press PLAY. L and R carry the same mono audio.

For a repeating sample, set LOOP to FWD. For a new recording, patch audio into AUDIO, raise REC, then lower it to finish. A completed take replaces the old sample.

OTIS can import captures from an adjacent Spoon Bender on its left and share completed sample memory with Vastish on its right. Enable each follow option in the receiving module's menu. Completed samples are embedded in saved Rack patches.

## Build

From the repository root, with the Rack SDK installed separately:

~~~sh
make -C vcv/otislab RACK_DIR=/absolute/path/to/Rack-SDK -j2
make -C vcv/otislab dist RACK_DIR=/absolute/path/to/Rack-SDK
~~~

The package is written to dist/ inside this plugin directory. Keep the repository's vcv/shared directory when building from source.

Linux live use has been reported. Windows and macOS builds are produced by CI; live use on those platforms remains unverified. Portable OTIS and Spoon Bender engine tests also run on Windows and Apple Silicon.

## License

BSD-3-Clause. Playback interpolation and filter arithmetic are adapted from MAME's ES5505/6 implementation by Aaron Giles. The included demo is procedural. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
