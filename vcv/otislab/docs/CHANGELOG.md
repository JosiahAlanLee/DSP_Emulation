# OTIS change history

## 2.11.4

- Initializing Otis Kit, or loading a patch or preset into it, now clears the
  last take's status. Before, a linked Otis Capture kept showing the old take's
  slot and length (or CUT or NO ROOM) on the empty bank, its ROOM could lag
  for a moment, and the next take's name continued the old count.

## 2.11.3

- Otis Kit no longer loses a take that lands in the same memory as the take
  last copied out of its slot. Before, saving, **Compact the bank**, **Load
  WAV**, **Clear** or **Load folder** could bring back the slot's older take
  when takes of equal length went into a nearly full bank.
- A take that finishes while the kit compacts or loads a WAV is kept. Only a
  take still in progress is dropped.
- Takes recorded at Rack's 705.6 kHz and 768 kHz engine rates now reload with
  the patch. The saved-rate limit is 1,000–768,000 Hz.
- A sample-rate change during a take drops the take, as Compact does, instead
  of landing it with the wrong pitch for the part recorded after the change.

## 2.11.2

- Re-storing an OTIS patch now applies its REV to voices already playing that
  patch. Clearing a stored patch applies the panel's REV to its voices at once.
  New hits still take their direction after any retrigger fade.

## 2.11.1

- Loading or compacting Otis Kit during a take now discards the take without
  recording its remaining tail. Capture's REC and DONE keep their timing.
- Capture latches whether a take starts with CLOCK. Removing CLOCK during a
  clocked take falls back to the TEMPO duration measured from its start;
  patching CLOCK during an unclocked take does not change its timing.

## 2.11.0

- New module, **Otis Capture** (8 HP), Otis Kit's capture companion: placed
  directly right of the kit, it records IN, the kit's own output, or both, for
  1/4 to 32 beats of its CLOCK (or of TEMPO), into a chosen slot. The take is
  written straight into free kit memory at Rack's sample rate and replaces the
  slot only when complete; a hit already sounding keeps the take it started
  with. See [CAPTURE.md](CAPTURE.md).
- Otis Kit saves takes like any slot, copying them out without stopping the
  audio thread, and gains **Compact the bank** in its menu.

## 2.10.0

- New module, **Otis Kit** (18 HP): a bank of eight mono sample slots packed
  into one ES5505 sample memory and played on OTIS's own voice code. Per slot:
  TUNE, DECAY, LEVEL, FILTER (K1), START and REV. GATE channel *n* plays slot
  *n*, or SLOT's bits 0-2 choose the slot per hit; PITCH and LEVEL CV are
  latched from the hit's channel. Load WAVs per slot or a whole folder; the
  bank is saved with the patch. See [KIT.md](KIT.md). Capture comes later.
- The engine gains a per-voice region and decay for the kit. Both are off for
  OTIS Sampler, whose 2.2.0 regression trace is unchanged.

## 2.9.0

- Patch select, the EPS idiom: bits 1 and 2 of the new PATCH input choose, on
  each hit, the panel or one of three patches stored from it. (From 2.11.x the
  docs number these bits 0 and 1, counting from zero: the same two lanes, cable
  channels 1 and 2.) A patch holds
  PITCH, START, END, K1, K2, LOOP, REV, INTERP, POSITION and SIZE, with CV
  added as on the panel. Store, recall and clear them from the menu; they are
  saved with the patch.
- PATCH is appended after FRAME BUS, so existing cable ids are unchanged.
  Unpatched, OTIS plays as 2.8.0 did: the 2.2.0 regression trace is unchanged.

## 2.8.0

- FRAME BUS selects complete frames from a byte, optionally clocked, without
  changing native stereo playback or the original parameter/cable IDs.
- Optional 2 ms stereo-linked frame crossfades retain playback offset and
  coalesce rapid requests. Hard switching remains available.
- Export the selected voice-1 frame as a verified Lookup JSON table, choosing
  left, right or mono mix. The patch saves the smoothing setting.
- Included a self-contained Register/Lookup/OTIS stereo frame example and
  portable/native/sanitized tests of the complete connection.

## 2.7.0

Stereo sample links. Captures from Spoon Bender 2.2.0+ import as stereo samples, and Vastish 2.4.0+ follows stereo samples in stereo, through a stereo extension placed after the existing share (older readers still see the mono mix). **Import Spoon Bender captures as mono (twice as long)**, saved with the patch, imports the mix instead. A capture longer than stereo memory is truncated and marked.

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
