# The emulated-instrument adapter

The Rack-side plumbing shared by the plugins that run an emulated Elektron instrument
in Rack: Machinemodule (the Machinedrum) and Monomodule (the Monomachine, Gorgon roadmap
C2). It holds what doesn't depend on which instrument it is:

| File | What it does |
| --- | --- |
| `src/Scheduling.hpp` | Hits, events and the blocks the audio thread hands the worker, the lock-free SPSC queue, Gorgon sound-link reception (`LinkState`), and per-hit control capture (`capture`, `resolve`) |
| `src/Renderer.hpp` | The worker-side renderer: events onto the emulator's 44.1 kHz native blocks, never early, then converted to Rack's rate; audio inputs converted the other way |
| `src/Worker.hpp` | The worker thread: loading the OS through a factory, generations, the bounded queues both ways, counters |
| `src/RackLink.hpp` | The Rack parts: the sound-link packet from a Gorgon on the left, and polyphonic per-track CV |
| `src/ResamplerCompat.hpp`, `src/ResamplerBridge.cpp` | Compile the core's resampler unmodified |

An instrument supplies a `Layout` (its track, output-channel and audio-input counts),
a `Backend` (the emulator: `fire`, `release`, `panic`, `render`, and, if it has inputs or
per-track settings, `input` and `configure`), and its module and panel. An instrument
without inputs, like Machinemodule, renders exactly as it did before inputs existed.
An explicit Initialize event calls the optional `Backend::initialize()` on the worker
before simultaneous hits; its default calls `panic()`. Monomodule overrides it to
restore fresh track state while retaining its loaded OS and DSP engines.

## The license boundary

Three kinds of code meet here. This is the boundary, written down before any code
moved here (roadmap C2a).

**BSD-3-Clause: `vcv/shared/`.** Contracts and helpers that BSD plugins (OTIS, Vastish,
ByteBusLab and the rest) and Gorgon also include: `GorgonSound.hpp` (Gorgon's
`spec/sound-link.hpp`, byte for byte), `ByteBus.hpp`, `PanelWidgets.hpp`,
`SampleFile.hpp`, `SampleShare.hpp`, the panel and glyph scripts. Nothing in
`vcv/shared/` includes AGPL or GPL code, so any plugin may use it.

**AGPL-3.0-only: `vcv/adapter/` (this directory).** It can't be BSD as a whole:

- `Renderer.hpp` compiles the resampler from shnolk's Monomodule (AGPL-3.0), which
  the pinned Machinemodule core carries in `desktop/src/core/Resampler.*`.
- It exists to be linked with the emulator cores. dsp56300 and gearmulator are
  GPL-3.0, and the cores built on Monomodule are AGPL-3.0. Every plugin that
  includes it is AGPL-3.0-only.

The rest of it (`Scheduling.hpp`, `Worker.hpp`, `RackLink.hpp`) was written in this
repository and copies no upstream code. It is AGPL-3.0-only because the repository's
owner licensed it that way. Moving those parts to BSD and into `vcv/shared/` would be
the owner's decision, made explicitly, not a side effect of this extraction.

Includes go one way: `vcv/adapter/` may include `vcv/shared/` (BSD into AGPL is
fine), never the reverse. A BSD plugin must not include anything from here.

**The instruments' own code: `vcv/machinemodule/` and `vcv/monomodule/`.**
Backends that include an emulator core (`Machinedrum.hpp`), firmware decoders,
sample banks, modules and panels. All AGPL-3.0-only, with their own notices.

Every source archive a plugin's packaging writes (`tools/source_bundle.py`) includes
this directory, as the AGPL requires for the binary it ships with.
