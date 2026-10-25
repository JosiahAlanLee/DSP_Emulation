# OTIS Sampler

A mono and stereo sampler for VCV Rack 2 with MAME-derived ES5505 playback arithmetic, per-voice chip filters and level control. LAB extensions add variable clocking, interpolation control, short-window scanning, frame stepping and background sample reworking.

**[Byte-bus frame control and Lookup export](docs/BYTE_BUS.md)** let logic
sequence TransWave-style frames and turn recorded or reworked frames into byte
tables. Try the included `examples/OTIS-byte-frames.vcv` stereo patch.

OTIS records audio or loads WAV files. It uses no Ensoniq firmware, operating system or commercial sample library. It is an independent sampling instrument, not a complete EPS-16+ recreation.

- **[User manual](docs/MANUAL.md)** — first sound, controls, patching, saving and troubleshooting.
- **[Installation and getting started](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md)** — every module, and the sample-sharing chain.
- **[Development and builds](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md)** — SDK setup, tests and source layout.
- **[Validation](docs/VALIDATION.md)** — reference comparisons and implementation limits. See also [change history](docs/CHANGELOG.md).

## Download and install

1. Download the zip for your platform from the [latest release](https://github.com/JosiahAlanLee/DSP_Emulation/releases): Windows x64, Linux x64, macOS Apple Silicon or macOS Intel. Each zip holds all the plugins.
2. In Rack choose **Help → Open user folder**, then quit Rack.
3. Copy `OtisLab-2.8.0-<platform>.vcvplugin` from the zip, unopened, into the `plugins-<platform>` folder there (for example `plugins-win-x64`). Copy the other modules as well to use the sample-sharing and byte-bus links.
4. Start Rack.

Exact file names for each platform are in [Getting started](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md#install). Linux has been used live in Rack; the Windows and macOS builds haven't been run inside Rack yet, so please [report](https://github.com/JosiahAlanLee/DSP_Emulation/issues) how they go. To build from source, see the [development guide](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md).

## First patch

Add **OtisLab → OTIS Sampler** (36 HP). Choose **Load included demo** from the right-click menu, patch L to your mixer or Audio module, and press PLAY. A mono sample plays the same on L and R.

For a repeating sample, set LOOP to FWD. For a new recording, patch audio into AUDIO L, raise REC, then lower it to finish. A completed take replaces the old sample.

Two-channel WAV files load as stereo samples, and a take is stereo when AUDIO R is patched. Each stereo voice is a linked pair of chip voices, so a stereo sample has half the memory and half the voices of a mono one. See [Stereo samples](docs/MANUAL.md#stereo-samples).

OTIS can import captures from an adjacent Spoon Bender on its left and share completed sample memory with Vastish on its right. Enable each follow option in the receiving module's menu. Completed samples are embedded in saved Rack patches.

## License

BSD-3-Clause. Playback interpolation and filter arithmetic are adapted from MAME's ES5505/6 implementation by Aaron Giles. The included demo is procedural. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
