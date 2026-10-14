# Validation and limits — 2026-10-14

## Completed checks

- Linux x86-64 VCV plugin built with Rack SDK 2.6.4 and GCC 13.3, warning-free.
- `plugin.so` loaded with `dlopen` against the actual SDK libRack. Its exported
  `init` registered the model and two independent instances were created.
- The compiled module produced deterministic nonzero audio, with equal L/R,
  after loading/restoring a sample through its Rack JSON API.
- Compiled-module playback gates triggered and stopped voices in gated mode.
- Compiled-module CV recording started on a high record gate and finished on
  a falling gate. Stored duration matched the conversion clock.
- 288,000 exact integer output comparisons against mardlib's independent
  standalone ES5505 core: four filter topologies × two directions × three
  loop settings, 12,000 ticks each. Random sample data, nonintegral frequency
  increment, fixed nontrivial coefficients; unity chip volume.
- Twelve FFT checks using a bright sine: four transpositions at each of
  44.1, 48 and 96 kHz host rates. Spectral peaks matched the expected alias
  after native-clock transposition to within 3 Hz. Measured values are in
  `spectral-results.json`.
- Capture duration checked at all seven recording clocks and all three host
  rates (21 combinations).
- Reverse one-shots stop, very short forward/ping-pong loops tolerate maximum
  pitch increments, output stays finite, and chip-mode voice limits apply.
- PCM/base64 and WAV export/import round trips preserve 16-bit sample data.
- Recording stops at capacity and swaps preallocated buffers.
- AddressSanitizer and UndefinedBehaviorSanitizer behavioral tests passed.
  LeakSanitizer was disabled because this runtime cannot enumerate the other
  process task directory; no leak check is claimed.
- SVG panel rasterized with the NanoSVG implementation shipped in the Rack
  SDK, visually inspected, and corrected for a clipped header label.

The Linux binary's highest imported glibc symbol version is 2.35. libstdc++
and libgcc are linked statically using Rack's build configuration. A genuine
older-Ubuntu compatibility test was not available.

## What this establishes

The reduced engine reproduces the selected integer interpolation and filter
operations of an existing ES5505 implementation under the tested conditions.
Chip-generated pitch aliases are retained when converting the completed audio
stream to a modern host rate. It establishes neither complete EPS behavior
nor a perceptual match to a physical sampler.

## Deliberate departures / unverified behavior

- No CPU, OS, keyboard firmware, instrument parser, original envelopes or
  original ES5505 logarithmic volume control. Host gain is linear.
- Input capture uses an approximate six-pole low-pass filter and clocked
  16-bit quantization; neither the EPS ADC nor its analog circuitry is modeled.
- WAV import quantizes/downmixes existing data at the file rate; it does not
  resample at the REC RATE knob setting. Use live capture for that behavior.
- Internal chip audio runs at `10 MHz / (16 × active voices)` with fixed
  output modes 21/14/8 hardware slots, exposed as 16/14/8 Rack channels. These
  clocks are independent of the host rate. Their output modes are not switched
  by an original EPS effects/OS program.
- Fractional interpolation uses 11-bit scaled coordinates with ES5505
  register-compatible increment granularity (equivalent to the chip's 9-bit
  addressing fraction); segment boundaries in this UI are whole samples.
- Guarded sample addresses and generalized loop folding intentionally avoid
  underflow and invalid addresses for very short loops / large increments.
  Extreme-loop behavior is therefore not claimed to reproduce silicon.
- Native-to-host conversion is a 64-tap, 1024-phase Blackman-windowed sinc
  approximation, not the original DAC/reconstruction filter. Its group delay
  is about 31 chip samples (1.04 ms at the default clock), in addition to host
  and interface latency. EOC is emitted at the chip crossing, before that
  delayed audio reaches the output.
- Monophonic sample memory; L and R are currently the same mixed signal.
- Polyphonic voices are mixed without automatic gain compensation. Many
  voices can exceed nominal ±5 V; use LEVEL and downstream attenuation.
- Trigger controls and CV-to-register updates are a new host design. Continuous
  CV can do things the original front panel could not.
- ESP effects, firmware extraction, and Max/Max for Live wrappers are deferred.
- No actual Rack GUI launch, file-dialog interaction, PipeWire/ADAT hardware
  test, listening comparison against a working EPS, or Mac/Windows build was
  performed in this environment. The plugin itself was exercised through the
  Rack API; the panel image is a rendered layout illustration.

## References used

MAME ES5505/6 by Aaron Giles (BSD-3-Clause):
https://github.com/mamedev/mame/blob/master/src/devices/sound/es5506.cpp
https://github.com/mamedev/mame/blob/master/src/devices/sound/es5506.h

Retrieved source SHA-256:

```text
4700095ef62bfb8c1e0487a093aeb6ac2a89c128466cf807d4703226ded07fd4 es5506.cpp
cc768f89ada506ec02bdf9ea5ea2a4a8bbc0967aad9363c998af9c24287e3bf6 es5506.h
```

Independent comparison and board conversion-clock interpretation:
https://github.com/mardlib/Ensoniq-EPS-16-Plus
commit `6230f38886d98c80edf39ef5cfeae3cf25a492d1`, particularly
`native/es5505_core.c` and `vst3/SamplingClockTest.c`.
Capture clock: `10 MHz / (112 × selector)`, selectors 8 through 2.

Rack API, voltage handling and module build:
https://vcvrack.com/manual/PluginDevelopmentTutorial
https://vcvrack.com/manual/VoltageStandards
https://vcvrack.com/manual/PluginGuide

Expert Sleepers ADAT interfaces:
https://www.expert-sleepers.co.uk/es3.html
https://www.expert-sleepers.co.uk/es6.html
https://www.expert-sleepers.co.uk/es7.html

## 2.1.0 changes

Checked 2026-10-14 by a second reviewer (Claude), separately from the 2.0.0 checks above.

- MAME `es5506.cpp` fetched independently (SHA-256 matches the hash above). Filter
  arithmetic, pole history and the four topologies were compared by reading; they match.
- The included reference comparison was rebuilt against mardlib commit `6230f38` and passes.
- An additional randomized comparison against the mardlib core: 4,000 trials, each with
  full-range random 16-bit data, random K1/K2, random filter topology, direction and loop
  mode, increments up to ~15.6x and random loop regions of 200–3,200 samples, 3,000 ticks
  each. 12,000,000 samples, zero mismatches. The same run with a random per-trial volume
  register applied through the new volume table also gives zero mismatches against the
  reference's left output.
- Note: the mardlib core and this engine both derive from MAME, so agreement validates the
  port, not hardware accuracy.
- Engine tests extended (volume table values, per-voice level ratio, declick release and
  retrigger, snapshot/sequence behaviour); pass normally and under ASan/UBSan.
- Concurrency stress: one thread finishing a short live recording every few microseconds,
  another snapshotting continuously for 3 s; ~21 M snapshots, zero torn copies. The lock
  fallback was needed only when retries were exhausted under that artificial churn.
- `src/plugin.cpp` and `src/OtisSampler.cpp` compile warning-free against Rack v2.6.4
  headers with Rack's compile flags. Not linked or loaded: libRack was not available in the
  build environment, and nothing was run inside Rack.
- Panel regenerated with `tools/make_assets.py` (VCA jack added; bottom row now seven jacks
  at 46 px spacing). The demo WAV regenerates byte-identically.
- The original 2.1.0 snapshot path read ordinary vector/metadata state while the
  audio thread could swap it. Its sequence counter did not make those accesses
  race-free under C++; the successful stress run above did not establish safety.
  This path is replaced in 2.1.1 below.

## 2.1.1 snapshot fix

- Finished sample pointer, length, rate and PCM copying share `sampleMutex`.
  An audio-thread publication only tries that lock: it retains the completed
  record buffer on failure, continues playback and retries in `process()`.
  Subsequent recording cannot overwrite that pending take; rearm/cancel is explicit.
- The sequence counter is retained only to invalidate the serialized PCM cache.
  Snapshot readers do not speculatively read mutable non-atomic state.
- The old engine-lock fallback in save/export is removed.
- Deterministic tests hold a snapshot lock while processing audio: playback remains
  nonzero, a finished capture is deferred, a queued start begins after publication,
  and a falling gate cancels the queue without losing the completed take.
- Concurrent reader/producer test verifies revision, sample rate, length and final
  PCM value across 200 completed takes. It passes normally, under ThreadSanitizer
  with no reports, and under AddressSanitizer/UndefinedBehaviorSanitizer. Leak
  checking is disabled here for the runtime restriction documented above.
- Portable engine tests pass; plugin links warning-free against Rack SDK 2.6.4.
- The actual compiled plugin's Rack API harness passes sample restoration,
  deterministic playback, CV recording, declicked gate release and optional hard
  gate release. The default fade is allowed to finish before checking silence.
- No live Rack GUI, PipeWire or ADAT hardware test is claimed.

## 2.2.0 LAB controls

- Regression: a scenario covering all three chip-clock modes, all four filter topologies,
  three host rates, eight transposed voices with per-voice K1/K2/VCA, a gated release,
  a retrigger and a live recording renders byte-identically with 2.1.1 when LAB controls
  are at their defaults.
- The randomized ES5505 comparison against the mardlib core (12 million samples, random
  volume registers) still shows zero mismatches.
- `tests/lab_test.cpp`: pitch held within 10 Hz of a 438 Hz tone across clock offsets of −2 to +1
  octaves; varispeed −1 octave halves the pitch and +0.5 octave raises it by √2; a 2.98 kHz
  tone at a 5.26 kHz clock folds to the predicted alias frequency; drop-sample (0 bits)
  transposition leaves −24.5 dB of non-fundamental energy versus −52 dB for chip
  interpolation; switching LAB and authentic paths stays within 3x the steady-state
  sample-to-sample change; NaN and extreme clock CV stay finite; resampling records the
  output within 10% RMS; per-voice START/END sets independent segments.
- All engine, snapshot and lab tests pass normally; engine and lab tests under ASan/UBSan;
  snapshot test under ThreadSanitizer.
- `src/OtisSampler.cpp` compiles warning-free against Rack v2.6.4 headers with GCC 13 and
  GCC 11 (the CI runner's compiler). The GitHub Actions workflow
  (`.github/workflows/otislab.yml`) builds and packages the plugin, runs these tests and the
  mardlib reference comparison, and loads `plugin.so` through libRack.
