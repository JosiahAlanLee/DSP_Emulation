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

## Gorgon: a CV and A/V tracker for Rack

[`vcv/gorgon`](vcv/gorgon) connects Rack to Gorgon, a CV and audio/video tracker in development in [HydraPy](https://github.com/JosiahAlanLee/hydrapy) Studio. One score drives the visuals, the modular and Rack, and Gorgon is the master clock.

- **Out of Rack's jacks:** clock, reset, run, gates, held 1 V/oct pitch and velocity (polyphonic), and the automation lanes. These are generated per sample on Rack's clock, which follows Studio's playhead. CV levels can be 10 V (Rack default) or 5 V (matching Studio's direct CV).
- **Into the visuals:** four CV inputs set the patch's named controls, and PLAY and RESET drive Studio's transport.
- **Audio:** four capture jacks receive Studio's optical soundtrack and synthesized breaks over JACK or PipeWire.
- **Set up:** Rack finds a running Studio on the same machine with no configuration.

For example: Gorgon's gates and pitch into OTIS and Vastish, through Spoon Bender, with the same score moving the picture.

Status: in development, built and tested in CI against a pinned HydraPy commit, not yet part of the release bundles. Physical audio/video latency and hardware CV routes are still being validated. [Manual](vcv/gorgon/docs/MANUAL.md).

## Also in this repository

- [`pd/vastish`](pd/vastish): the original Vastish, a VAST-style eight-voice synth in vanilla Pure Data (0.52+).
- [Development guide](docs/DEVELOPMENT.md): building from source, the Rack SDK, tests and how the engines are validated.

Kurzweil, VAST, Ensoniq and EPS are their owners' trademarks. These are independent emulations, not affiliated with or derived from manufacturer code, firmware or samples. OtisLab's playback arithmetic is adapted from MAME (BSD-3-Clause, Aaron Giles); see its THIRD_PARTY_NOTICES.
