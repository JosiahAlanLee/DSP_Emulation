# OTIS Sampler — first working prototype

> **2.3.0 (2026-10-14):** adds fixed-size loop scanning, Transwave-style frame
> stepping, and a triggerable background loop-reworking experiment. New features
> are original LAB extensions inspired by ASR-10 workflows. The panel is now
> **36 HP**; old parameter and cable IDs are preserved. Legacy mode remains the
> default and matches 2.2.0 in the regression trace. See [Window and rework](#window-and-rework-230).
> This is still an ES5505-based sampler, not an ASR-10 or ESP effects emulator.
>
> **2.3.1:** REWORK now RMS-matches its result to the source region, so repeated
> generations keep their level instead of losing ~4.5 dB each time.
>
> **2.3.2:** fixes a crash when the module browser or panel draws OTIS. The
> display requested `DejaVuSansMono.ttf`, which Rack 2 does not ship; it now uses
> Rack's ShareTechMono and falls back safely if a font fails to load.
>
> **2.3.3:** panel layout fix. Switches and buttons now sit below their labels
> instead of on top of them, and LOOP and MODE show each position's name
> beside the lever stop (top to bottom: PING/FWD/OFF, FRM/SCAN/SEG).


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
For current versions, use the plugin from a successful GitHub Actions run (artifact **OtisLab-lin-x64**), the supplied delivery ZIP, or build from source and use the `.vcvplugin` in `dist/`. The supplied Linux x86-64 build requires Rack 2 and glibc 2.35 or newer (Ubuntu 22.04+).
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

## LAB controls (2.2.0)

These are extensions, not EPS behaviour. Version 2.2 introduced the CLOCK/INTERP column at 26 HP; version 2.3 expands the panel to 36 HP.
At their defaults (CLOCK 0, INTERP 9, record source AUDIO) output is bit-identical to 2.1.1.

| Control | Behavior |
|---|---|
| **CLOCK** | Chip clock offset in octaves from the selected chip-clock mode (−6 to +3), plus **CLK CV** at 1 V/oct. Polyphony stays with the mode. Range is clamped to 1 kHz – min(4 × host rate, 400 kHz). A 0.005-octave dead band around 0 returns to the authentic path; double-click the knob to reset |
| CLOCK knob moves (menu) | **Resolution only** (default): pitch is held, so lowering the clock folds content above the new Nyquist back down and raising it cleans transposition up. **Varispeed**: pitch follows the clock, like a tape or sampler clock change. In resolution mode, pitch increments still clamp at the chip's ~32x register limit, so very low clocks with high sample rates can go flat |
| **INTERP** | Fraction bits used between samples, 0–9, plus **INT CV** (about 1.1 V per bit). 9 is the chip; 0 is drop-sample playback, which adds stepped, zippery transposition artifacts |
| **START / END CV** | Now polyphonic: each voice gets its own segment and loop points. Modulating loop points at audio rate per voice gives transwave/wavetable-style scanning |
| Record source (menu) | **AUDIO input** (default), **Own output** (resample: record what OTIS is playing, through the capture filter and 16-bit quantization), or **AUDIO + own output**. Recording the module's own output and replaying it repeatedly accumulates generation loss |

Notes:
- The LAB clock path uses its own output converter (nine precomputed anti-alias cutoffs,
  512 phases); switching between it and the authentic converter hands over the filter
  history so there is no click. Kernels are built off the audio thread.
- K1/K2 and VCA CV were already sampled every host sample since 2.1.0, so audio-rate
  filter-coefficient and volume-table modulation needs no extra mode: patch audio into
  K1/K2 or VCA.

## Window and rework (2.3.0)

Three extra LAB controls use the existing START/END region as their source.
The original controls remain in the same positions; the added columns expand
the module from 26 to 36 HP, so existing patches may need spacing adjustments.

| Control | Behavior |
|---|---|
| MODE = Legacy segment (0) | Original 2.2 playback bounds. POSITION and SIZE have no effect. This is the default for new and older patches. |
| MODE = Fixed-size scan (1) | SIZE selects a window inside START/END. POSITION moves that window without resizing it. |
| MODE = Frames (2) | The region is divided into complete SIZE-sample frames. POSITION selects one frame at a time; incomplete trailing data is not selected. |
| POSITION / POS CV | 0–100% knob plus polyphonic 0–10 V CV across the valid window positions or frames. Selection rounds to the nearest sample/frame. |
| SIZE / SIZE CV | 2–65,536 samples, controlled logarithmically; default 256. Each volt doubles the sample count. SIZE CV is polyphonic; mono CV broadcasts. Right-click SIZE for exact sample-count entry. Window size is limited by the selected region. |
| COARSE / FINE | Controls randomized grain size for REWORK: coarse uses longer grains, fine uses shorter grains. Latched on trigger. |
| AMOUNT / AMT CV | Original/processed sample blend, 0–100%; CV adds 0–100% at 0–10 V. Latched on trigger. Zero amount leaves PCM unchanged. |
| REWORK / TRIG | Button or rising gate (2 V) reworks the selected **voice-1** window. The result changes that shared sample region for every voice reading it. |

Select LOOP = Forward or Ping-pong for repeating windows. With LOOP Off, a gate
plays the selected window once. Reverse remains available. Moving a window
preserves the current offset within it; position changes do not restart phase.
Changing SIZE wraps that offset into the new size. Window movement and frame
selection are intentionally unsmoothed, so CV jumps can make clicks or zipper
artifacts. There is no automatic crossfade between scanned frames.

LAB windows use half-open bounds and wrap interpolation within the selected
frame in forward-loop mode, avoiding a read from the next frame. This differs
from the ES5505's legacy end-register semantics; that path is unchanged. The
existing EOC output still emits 1 ms pulses, so very short loops can keep it high.

REWORK is an original algorithm, **not** a reverse-engineered implementation of
Ensoniq's Synthesized Loop. It uses deterministic randomized circular grains,
windowed overlap/add, loop-seam smoothing, and the AMOUNT blend. It adds no new
noise source. The blended region is RMS-matched to the source region (gain
limited to ±12 dB), so repeated operations keep their level; they still smear
transients and darken the spectrum, and peaks can rise toward full scale as the
crest factor changes (int16 hard-clips). Each accepted request advances the
random seed.
Samples outside the selected window remain unchanged.

A worker copies and processes the finished sample while playback continues.
The audio thread publishes a completed result with a 5 ms PCM crossfade before
the chip filters; it does not reset voices or output-converter history. If an
export/save currently pins the finished sample, publication waits without
blocking playback. A new recording/load invalidates stale results. Triggers
while BUSY are ignored, not queued. The menu can cancel a pending job; Reset
also cancels pending work. An already applied result is not undone by Cancel.

The processed PCM and all window parameters are saved with the Rack patch.
Pending jobs and the random-seed progression are not serialized. REWORK replaces
the selected region, so export a take if you want to retain that generation.
There is no continuous delay/overdub buffer or ASR effects processor in this build.

### First patch

1. Load the included demo or your own WAV, turn LOOP to Forward, and trigger PLAY.
2. Set MODE to Scan and SIZE to 256 samples; sweep POSITION or feed POS CV.
3. Change MODE to Frames to hear selection move by complete waveform frames.
4. Press REWORK at AMOUNT 50%; wait for BUSY to finish, then scan the changed region.
5. Try CLOCK and INTERP alongside the window controls. For polyphonic regions,
   send separate POS/SIZE CV channels with your polyphonic pitch/gate source.

The delivery's `audition/` directory contains a purpose-made 16-frame source,
matching continuous/stepped scans, and four successive loop-rework operations.
These are native offline renders. Rework generations are level-matched by the
algorithm itself (2.3.1); no other normalization is applied.

The historical design references are Ensoniq's [ASR-10 Musician's Manual](https://www.samplekings.com/pdfs/ENSONIQ_ASR10%20User%20Manual.pdf),
Edit/Wave modulation (PDF page 183) and Synthesized Loop (PDF page 192).
No Ensoniq program code was used for the new LAB algorithms.
