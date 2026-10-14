# OTIS Sampler — first working prototype

> **2.1.1 in this repo (2026-10-14):** fixes the 2.1.0 sample snapshot data race
> while retaining its per-voice VCA, polyphonic filter CV and optional declick fades.
> Finished samples are copied under a separate lock; the audio thread keeps playing
> and defers capture publication instead of waiting. Build this version from source;
> the original 2.0.0 prebuilt binary predates these changes.

A VCV Rack 2 module for sampling a modular system and playing samples through
MAME-derived ES5505 integer interpolation and filter arithmetic. Built for an
existing audio/CV setup such as Digi003 → ES-3/ES-6/ES-7 over ADAT on Ubuntu.
It does not need Ensoniq ROMs or a working EPS.

This is an audio-engine experiment, not a complete EPS-16+ emulator. Original
ESP effects, EPS envelopes, instrument formats and the original input/output
analog circuits are not included. The scope and numerical validation are in
[docs/VALIDATION.md](docs/VALIDATION.md).

## Install a Linux build

The original 2.0.0 delivery ZIP contained a prebuilt `OtisLab-2.0.0-lin-x64.vcvplugin`.
For 2.1.1, build from source (below) and use the `.vcvplugin` from `dist/`. That binary targets Rack 2 on Linux x86-64, glibc 2.35 or newer (Ubuntu 22.04+).
It was built against Rack SDK 2.6.4. Older systems should build from source.

1. In Rack, use **Help → Open user folder**, then quit Rack.
2. Place the `.vcvplugin` file in that folder's `plugins-lin-x64` directory.
   Create the directory if it is absent. Keep the `.vcvplugin` file intact.
3. Restart Rack. Search the module browser for **OTIS Sampler** or **OtisLab**.
4. Right-click the module and select **Load included demo**, or open
   `examples/OTIS-start.vcv`, which already embeds the demo sample.
5. Patch **L** into your existing Rack audio output path. Press **PLAY**, then
   change **PITCH**. To use the starter inside a working patch, select/copy its
   sampler and paste it into your existing patch.

No existing PipeWire settings, device routing or audio interface configuration
are changed by this package. There is no installer script.

## First hardware patch

Use the same Rack Audio interface and channel mapping that already work with
your Digi003 and ES modules. Do not assume that an ADAT channel is channel 1.

| Existing signal path | Connect to / from OTIS |
|---|---|
| Modular audio → ES-6/7 → ADAT input → Rack | **AUDIO** input |
| Modular pitch CV → ES-6/7 → Rack | **V/OCT** input |
| Modular trigger/gate → ES-6/7 → Rack | **GATE** input |
| Modular record gate → ES-6/7 → Rack | **REC** input |
| OTIS **L** → Rack audio output → ADAT → ES-3 | Modular audio return |
| OTIS **EOC** → Rack output → ADAT → ES-3 | Optional loop/end trigger |
| OTIS **POS** → Rack output → ADAT → ES-3 | Optional position CV |

Keep the voltage scaling you have already validated with ordinary Rack CV
modules. Within OTIS, 1 V changes pitch by one octave and 0 V means original
sample pitch. Audio capture expects ±5 V for full scale. Gate thresholds are
0.1 V low / 2 V high. EOC is a 10 V, approximately 1 ms pulse.

For capture, patch an audio source to **AUDIO**. Raise **REC**, or hold its CV
input high. Lower it to finish. The completed recording replaces the old sample
and stops its voices; **PLAY** or a new gate edge starts the new sample. While
recording, the previous sample can continue to play. Record gain clips into
16-bit memory at full scale. There is no automatic normalization.

## Controls

| Control | Behavior |
|---|---|
| **PITCH** | ±48 semitone offset; continuous, including register quantization |
| **START / END** | Playback segment and loop boundaries, 0–100% |
| **K1 / K2** | Raw normalized chip filter coefficients; these are not cutoff Hz |
| **REC RATE** | Seven conversion-clock rates, table below |
| **REC GAIN** | Capture gain, 0–4× |
| **LEVEL** | Output gain, 0–2× |
| **PLAY** | Retrigger voice 1 |
| **REC** | Record while high; panel switch and CV are ORed |
| **LOOP** | Off / forward / ping-pong, bottom to top on the switch |
| **REV** | Reverse initial direction; changing it also changes active voices |
| **START / END CV** | Adds CV/10 to the knob value, then clamps to 0–1; bipolar CV works. Mono (all voices) |
| **K1 / K2 CV** | Polyphonic: channel *n* sets voice *n*'s coefficient (knob + CV/10, clamped 0–1). A mono cable applies to every voice |
| **VCA** | Polyphonic per-voice level through the chip's own 8-bit log volume register. 0–10 V; 10 V = unity; unpatched = unity. Default response is chip log (CV linear in register, ~6 dB per 0.52 V, so a linear ADSR gives a dB-linear envelope); the context menu offers linear amplitude, still stepped through the chip table |
| **L / R** | Identical mono audio for now; reserved for later stereo effects |
| **EOC** | Combined pulse at any voice's end/loop crossing |
| **POS** | First active voice's sample position, 0–10 V |

Pitch and gate cables can be polyphonic. A mono pitch or gate signal broadcasts
to the other cable's channels. Default gate behavior is one-shot: release does
not stop a voice. Enable **Gate release stops playback** in the context menu
for a gated response. Removing a poly gate cable releases voices only in gated
mode; the context menu's **Stop all voices** always stops them.

The capture rate is latched when recording begins. Changing the knob during
recording affects the next capture. Maximum mono memory is 1,048,320 samples
(just under 2 MiB). WAV files beyond that capacity are truncated and marked in
the display. On reaching capacity, recording finishes automatically; lower the
record controls before rearming. The small red **FULL** indicator lights.

| Capture position, left to right | Rate | Maximum duration |
|---|---:|---:|
| 1 | 11,160.714 Hz | 93.93 s |
| 2 | 12,755.102 Hz | 82.19 s |
| 3 | 14,880.952 Hz | 70.45 s |
| 4 | 17,857.143 Hz | 58.71 s |
| 5 | 22,321.429 Hz | 46.96 s |
| 6 (default) | 29,761.905 Hz | 35.22 s |
| 7 | 44,642.857 Hz | 23.48 s |

### Context menu

- Load a WAV (PCM 8/16/24/32 or float 32/64, including corresponding extensible WAV).
  Multichannel WAVs are averaged to mono, quantized to 16-bit and retain their
  file sample rate. Loading does **not** simulate recording through the EPS ADC.
- Load the included original synthetic demo.
- Export the finished sample as a 16-bit mono WAV.
- Select gated playback.
- Declick release/retrigger (on by default): gated release and retriggering a sounding voice
  fade over ~1.5 ms of chip time instead of cutting. A retrigger restarts after the fade,
  so onsets are delayed by that amount. Turn off for the original hard cuts.
- VCA CV response: chip log (default) or linear amplitude.
- Select the chip output clock: 29.76 kHz (up to 16 Rack voices), 44.64 kHz (14),
  or 78.125 kHz (8). Lowering the voice limit stops excess voices.
- Select four chip filter topologies.

Sample data, sample rate and settings are embedded in the Rack patch. This also
works for live recordings and portable presets; it does not depend on the
original WAV path. Save after finishing capture: an in-progress take is not
saved. Patches can contain up to about 2.8 MB of sample JSON per instance before
Rack compression. Saving/exporting copies only the finished sample under a
separate sample lock, so playback continues during the copy. If capture finishes
then, its publication is deferred until the copy releases the buffer. A new record
start is queued behind that publication; a falling record gate cancels the queued
start. File import/patch restore still synchronize with the DSP and can briefly
interrupt audio, so avoid those operations during an uninterrupted live take.

## Audition

`examples/pitch-audition.wav` is rendered by this engine at 48 kHz. It plays the
same bright demo at **−24, −12, 0, +12 and +24 semitones**, two seconds each,
with quarter-second silences. This is synthetic software output, not a
recording of physical Ensoniq hardware. The demo deliberately includes bright
partials and noise for evaluating transposition.

## Build from source

Download the appropriate Rack SDK separately from
https://vcvrack.com/downloads/ and unpack it in a path without spaces.
Install a C++17 compiler, GNU Make, `jq` and `zstd` using your platform's usual
tools. On Ubuntu, `build-essential jq zstd unzip` are the relevant packages.

```sh
cd OtisLab
make -j2 RACK_DIR=/absolute/path/to/Rack-SDK
make dist RACK_DIR=/absolute/path/to/Rack-SDK
```

The distributable is in `dist/`. The source uses Rack's standard SDK build
system. The provided binary and tests cover Linux x64 only; macOS/Windows
builds are possible targets but have not been built or verified here. No Max
wrapper is included yet. The portable core in `src/engine/` does not include
Rack headers and can be reused for one.

## Development checks

```sh
make -C tests test
make -C tests sanitize
```

If LeakSanitizer is unavailable in a restricted environment, run the already
compiled sanitized executable from the project root:
`ASAN_OPTIONS=detect_leaks=0 tests/build/engine_test_sanitized`.
See `tests/README.md` for Rack ABI and spectral checks.

The generated panel uses outlined lettering for Rack's SVG renderer. The
illustration in `docs/panel-preview.png` is a layout preview, not a screenshot
of a running Rack session.

## Next engineering step

Try the supplied demo and a familiar bright sample on your existing modular
patch. Report pitch setting, chip clock, host rate and which artifact differs
from your expectations. This tests whether the selected playback model is the
one you want. Original ESP effects need a separate execution core plus effect
programs and initialization data; they are not simulated by a generic reverb.

## Changes in 2.1.0

- **Per-voice level (VCA input).** Voices are still summed to one output, so external
  envelopes previously could only shape the whole mix. Each voice now passes through the
  ES5505's 8-bit log volume table (4-bit exponent, 4-bit mantissa, as in MAME
  `compute_tables(8,4,4)`), with register 0xC0 = exact unity. CV never drives a voice above
  unity. Patch a polyphonic envelope (one channel per gate channel) into VCA for per-note
  amplitude shaping.
- **Polyphonic K1/K2 CV.** Per-voice filter envelopes are now possible.
- **Declick fades** (host feature, not chip behaviour), described above.
- **Cached sample serialization.** Rack calls `dataToJson` on every autosave. The encoded
  sample is reused until it changes. The original 2.1.0 snapshot used only a sequence counter
  and contained a C++ data race; 2.1.1 replaces it with synchronized buffer ownership while
  keeping playback independent of the copy (see below).
- New input id `LEVEL_IN` is appended after the existing inputs, so 2.0.0 patches load with
  their cables intact. New settings (`linearLevel`, `declick`) default to off/on when absent.

Verification for these changes is in [docs/VALIDATION.md](docs/VALIDATION.md#210-changes).

## Changes in 2.1.1

- Replaced the sequence-counter-only snapshot with a mutex protecting finished
  sample metadata and PCM ownership. The counter remains a cache revision.
- Audio-side publication uses `try_lock`; if a copy is active, the completed take
  stays in its preallocated buffer and playback continues. Record restarts are
  queued safely, with cancellation on release.
- Removed the save/export fallback that held the processing lock while copying.
- Updated the compiled Rack test to check both the default release fade and the
  optional hard cut, including silence after converter latency.
- Added deterministic pinned-buffer and concurrent-generation regression tests,
  plus a `make -C tests thread-sanitize` target.
