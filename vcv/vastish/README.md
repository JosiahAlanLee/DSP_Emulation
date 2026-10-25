# Vastish

An eight-voice synthesizer for VCV Rack 2, with six DSP routing algorithms, fourteen processing functions, sample playback, three envelopes, two LFOs and four FUN modulation processors. Four AUX CV inputs bring external modulation into each voice. Envelope, LFO and FUN outputs can drive other Rack modules.

Developed from this repository's [Pure Data Vastish](https://github.com/JosiahAlanLee/DSP_Emulation/tree/main/pd/vastish) project. Pd is not needed to run the Rack plugin. The instrument uses original programs and no Kurzweil firmware or sample ROM.

- **[User manual](docs/MANUAL.md)** — first sound, controls, patching, saving and troubleshooting.
- **[Installation and getting started](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md)** — every module, and the sample-sharing chain.
- **[Development and builds](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md)** — SDK setup, tests and source layout.
- **[Validation](docs/VALIDATION.md)** — reference comparisons and implementation limits.

## Download and install

1. Download the zip for your platform from the [latest release](https://github.com/JosiahAlanLee/DSP_Emulation/releases): Windows x64, Linux x64, macOS Apple Silicon or macOS Intel. Each zip holds all the plugins.
2. In Rack choose **Help → Open user folder**, then quit Rack.
3. Copy `VastishLab-2.3.1-<platform>.vcvplugin` from the zip, unopened, into the `plugins-<platform>` folder there (for example `plugins-win-x64`). Copy the other two as well to use the sample-sharing links.
4. Start Rack.

Exact file names for each platform are in [Getting started](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md#install). Linux has been used live in Rack; the Windows and macOS builds haven't been run inside Rack yet, so please [report](https://github.com/JosiahAlanLee/DSP_Emulation/issues) how they go. To build from source, see the [development guide](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md).

## First patch

Add **VastishLab → Vastish** (56 HP). Patch a pitch/gate source to V/OCT and GATE, and L/R to your mixer or Audio module. The initial program is Ladder Bass; click its name to select another starting program. The sample and all parameter edits save in the Rack patch.

Vastish directly to the right of OTIS can follow its completed sample. Enable **Follow OTIS on the left** in Vastish's right-click menu. This shares stored audio; OTIS's playback settings remain separate.

## License

BSD-3-Clause. Includes DSP adaptations from Pure Data by Miller Puckette and contributors. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
