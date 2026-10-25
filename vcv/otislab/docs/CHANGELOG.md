# OTIS change history

## 2.6.0

Added stereo samples. Each Rack voice plays a stereo sample as a linked pair of chip voices with the same registers, one per channel, so L and R stay sample-locked and each equals a mono playback of its channel. A stereo sample holds half the memory per channel and gets half the voices (8, 7 or 4 by clock mode).

- **Load WAV…** keeps two-channel files stereo; **Load WAV as mono…** averages them as before.
- A new **AUDIO R** input (appended, so existing cable ids are unchanged) makes a take stereo when patched at its start; resampling a stereo sample's own output is stereo too.
- Export writes stereo WAVs. Patches save the right channel as `pcm16le_r`; earlier versions open them with the left channel.
- REWORK applies the same grains and one gain to both channels.
- Vastish receives a mono mix of a stereo sample. Spoon Bender captures remain mono.
- The mono path is unchanged: the 2.2.0 regression trace, the ES5505 reference comparison and a 300-scenario before/after comparison are identical.

## 2.5.2

Added the shared panel mark. No DSP changes.

## 2.5.1

Updated the capture link for Spoon Bender's plugin and module identifiers.

## 2.5.0

Added one-shot import and optional following of captures from Spoon Bender on the left. The follow setting saves with the Rack patch.

## 2.4.0

Published completed sample memory for a compatible Vastish immediately to the right. Vastish 2.2.0+ can follow file loads, recordings and REWORK results.

## 2.3.0–2.3.4

Added fixed-size scanning, frame stepping and background loop reworking. The panel grew from 26 to 36 HP. Subsequent fixes added REWORK level compensation, safe font loading, panel-label spacing and tapered region boundaries. The legacy playback path remains the default.

## 2.2.0

Added CLOCK and INTERP controls, per-voice START/END CV and self-resampling. The LAB controls extend the playback model; their defaults retain the baseline path.

## 2.1.1

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

## 2.1.0

- **Per-voice level (VCA input).** Voices are still summed to one output, so external
  envelopes previously could only shape the whole mix. Each voice now passes through the
  ES5505's 8-bit log volume table (4-bit exponent, 4-bit mantissa, as in MAME
  `compute_tables(8,4,4)`), with register 0xC0 = exact unity. CV never drives a voice above
  unity. Patch a polyphonic envelope (one channel per gate channel) into VCA for per-note
  amplitude shaping.
- **Polyphonic K1/K2 CV.** Per-voice filter envelopes are now possible.
- **Declick fades** (host feature, not chip behaviour), described in the [manual](MANUAL.md).
- **Cached sample serialization.** Rack calls `dataToJson` on every autosave. The encoded
  sample is reused until it changes. The original 2.1.0 snapshot used only a sequence counter
  and contained a C++ data race; 2.1.1 replaces it with synchronized buffer ownership while
  keeping playback independent of the copy (see below).
- New input id `LEVEL_IN` is appended after the existing inputs, so 2.0.0 patches load with
  their cables intact. New settings (`linearLevel`, `declick`) default to off/on when absent.

Verification for these changes is in [validation](VALIDATION.md#210-changes).
