# DSP_Emulation

Three modules for VCV Rack 2, built around early digital instruments and usable on their own or as one chain:

| Module | What it does | Manual |
|---|---|---|
| **OTIS Sampler** | Records or loads a mono sample and plays it through an ES5505-derived engine, with variable chip clock, short-loop scanning and destructive loop reworking. | [OTIS manual](vcv/otislab/docs/MANUAL.md) |
| **Vastish** | Eight-voice synthesizer in the style of Kurzweil VAST: three DSP blocks per voice, six routings, envelopes, LFOs, FUN processors, and sample playback from OTIS. | [Vastish manual](vcv/vastish/docs/MANUAL.md) |
| **Spoon Bender** | Stereo echo with a frequency shifter in its feedback loop, an eight-line reverb with freeze, an optional ES5510-style fixed-point engine, and CAPTURE, which turns the last few seconds into an OTIS sample. | [Spoon Bender manual](vcv/spoonbender/docs/MANUAL.md) |

![Spoon Bender, OTIS Sampler and Vastish side by side in VCV Rack](docs/images/three-modules.png)

Placed side by side as **Spoon Bender → OTIS → Vastish**, they pass samples along: a Spoon Bender capture becomes an OTIS sample, and OTIS's sample becomes a Vastish oscillator. Audio still travels through cables.

## Download

**[Latest preview release](https://github.com/JosiahAlanLee/DSP_Emulation/releases)**: one zip per platform, each holding all three plugins and an INSTALL.txt.

| Platform | File |
|---|---|
| Windows x64 | `DSP_Emulation-<release>-Windows-x64.zip` |
| Linux x64 | `DSP_Emulation-<release>-Linux-x64.zip` |
| macOS Apple Silicon (M1 and later) | `DSP_Emulation-<release>-macOS-AppleSilicon.zip` |
| macOS Intel | `DSP_Emulation-<release>-macOS-Intel.zip` |

Install steps and exact file names: **[Getting started](docs/GETTING_STARTED.md)**.

Status: Linux has been used live in Rack. Windows and macOS builds compile and pass the portable engine tests in CI but nobody has run them inside Rack yet; reports are welcome in [Issues](https://github.com/JosiahAlanLee/DSP_Emulation/issues). The modules aren't in the VCV Library yet.

## Also in this repository

- [`vcv/gorgon`](vcv/gorgon): Gorgon, a Rack bridge to the Gorgon tracker in [HydraPy](https://github.com/JosiahAlanLee/hydrapy) Studio: clock, held 1 V/oct pitch, gates and automation out, CV back into the visuals. Built in CI but not yet part of the release bundles. [Manual](vcv/gorgon/docs/MANUAL.md).
- [`pd/vastish`](pd/vastish): the original Vastish, a VAST-style eight-voice synth in vanilla Pure Data (0.52+).
- [Development guide](docs/DEVELOPMENT.md): building from source, the Rack SDK, tests and how the engines are validated.

Kurzweil, VAST, Ensoniq and EPS are their owners' trademarks. These are independent emulations, not affiliated with or derived from manufacturer code, firmware or samples. OtisLab's playback arithmetic is adapted from MAME (BSD-3-Clause, Aaron Giles); see its THIRD_PARTY_NOTICES.
