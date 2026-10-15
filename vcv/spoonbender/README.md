# Spoon Bender

A stereo echo and reverb for VCV Rack 2. Its frequency shifter can move each echo once or accumulate an offset inside the feedback loop. FREEZE holds a reverb tail. CAPTURE turns the recent output into a sample for OTIS.

CLEAN runs at Rack's sample rate. ESP combines reduced-rate processing with selected 24-bit node and 16-bit memory quantization. These are original algorithms; no Ensoniq effects microcode is included or executed.

- **[User manual](docs/MANUAL.md)** — first sound, controls, patching, saving and troubleshooting.
- **[Installation and getting started](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md)** — all three modules and their sample-sharing chain.
- **[Development and builds](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md)** — SDK setup, tests and source layout.
- **[Design specification](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/vcv/spoonbender/SPEC.md)** and [change history](docs/CHANGELOG.md).

## Download and install

1. Download the zip for your platform from the [latest release](https://github.com/JosiahAlanLee/DSP_Emulation/releases): Windows x64, Linux x64, macOS Apple Silicon or macOS Intel. Each zip holds all three plugins.
2. In Rack choose **Help → Open user folder**, then quit Rack.
3. Copy `SpoonBenderLab-2.1.1-<platform>.vcvplugin` from the zip, unopened, into the `plugins-<platform>` folder there (for example `plugins-win-x64`). Copy the other two as well to use the sample-sharing links.
4. Start Rack.

Exact file names for each platform are in [Getting started](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md#install). Linux has been used live in Rack; the Windows and macOS builds haven't been run inside Rack yet, so please [report](https://github.com/JosiahAlanLee/DSP_Emulation/issues) how they go. To build from source, see the [development guide](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md).

## First patch

Add **SpoonBenderLab → Spoon Bender** (32 HP). Patch audio to IN L, or IN L/R for stereo, and OUT L/R to your mixer or Audio module. IN R receives IN L when unpatched.

Set global MIX to 0% to hear the input, then raise it to hear the stage mixes. ECHO MIX controls the echo blend feeding the reverb; SPACE MIX controls the reverb blend. SHIFT at zero gives unshifted repeats.

Place OTIS directly to the right to import a capture. Spoon Bender saves its settings, but not live tails or capture history; import captured audio into OTIS and save the Rack patch to retain it.

## License

BSD-3-Clause. Arithmetic tests include excerpts from MAME's ES5510 core by Christian Brunschen. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
