# CZ-101 recording comparison

The committed capture archive contains **inputs, not recordings**. These tools
prepare a recording session and measure files returned from it. They do not
calibrate the plugin, adjust a keyboard, or establish either provisional CZ
converter law as correct. No keyboard recordings were available while writing
this workflow. Yamaha's word format already has exhaustive reference tests;
this capture set concerns the CZ-101.

## Prepare

Use a checkout containing the merged CZish source and a C++17 compiler. Rack
and its SDK are not needed for these offline tools. Python 3.10 or newer is
required for the supported NumPy/SciPy packages.

```sh
make -C vcv/czish/tests render
python3 -m venv /tmp/cz-measure-env
/tmp/cz-measure-env/bin/python -m pip install -r vcv/czish/tools/calibration-requirements.txt
/tmp/cz-measure-env/bin/python vcv/czish/tools/calibrate.py prepare /tmp/cz-session \
  --only a-silence b-cosine-level-50 c-bursts --rate 96000
```

Those three takes are a short first pass for recording level, noise, transfer
timing and burst intervals. For the 18-case converter/filter subset, omit
`--only`; for all 98 fixtures use `--set full`. A full run includes long
envelope clips of up to 120 seconds before padding. Preparing all three models
at 96 kHz takes roughly 1.5 GB for the full set; choose a persistent destination
with sufficient space. Existing nonempty directories are refused.

Each session contains:

- `inputs/`: the original tone SysEx, **padded** matching MIDI and sample-timed
  schedules. The MIDI starts with two seconds of silence and has at least two
  seconds after its last note-off. The original unpadded archive remains
  unchanged.
- `references/`: mono 32-bit float WAVs for the raw chip sum at 40 kHz, and
  the instantaneous and syllabic candidates with band-limited HOLD reconstruction
  at the requested rate. `--reconstruction clean` prepares CLEAN instead;
  the choice is recorded in the metadata. HOLD retains the device's lower
  sampling images. Every note-on, note-off and chord follows the matching MIDI;
  these are not the legacy renderer's single note held for 65% of a file.
  Reference gain is **1**, with no audition gain, noise, tracking error or
  post-converter tone filter. No hard clipping or 16-bit file quantization is
  applied. The frontend remains the provisional software frontend.
- `recordings/`: initially empty. Put each hardware WAV here under its fixture
  name, such as `b-cosine-level-50.wav`.
- `session.json`: chain metadata, expected rate, per-take settings, note times,
  fit/holdout assignment, source commit/dirty state, renderer and input/reference
  hashes. Analysis records hashes of the returned WAVs and session too.

The native render deliberately omits analog reconstruction. Analysis resamples
it with a band-limited polyphase filter when needed. It is a digital reference,
not a prediction of the exact measured line output. The two converted renders
also omit the keyboard's unknown analog filter and noise.

## Record

1. Fill in the `chain` fields in `session.json`: keyboard, audio interface,
   input type/gain, keyboard output setting, current trim status and any
   processing. Confirm `processing` as `none` if applicable. No physical trim
   is required by this workflow. Keep the untouched state as its own session.
2. Load each `inputs/NAME.syx` using a CZ-compatible librarian. The files are
   tone dumps; the librarian handles the instrument handshake. Put the
   keyboard in poly mode, then play **this session's** `inputs/NAME.mid`.
   The MIDI contains notes, not a tone upload. It supplies eight-note chords
   where required.
3. Record line output at 24-bit PCM or 32-bit float with headroom. Preserve
   the leading silence and the full tail. Use fixed interface/output gains
   across the set, without normalization, effects or noise removal. The
   recorder can start before the clip; automatic alignment permits an offset
   of up to five seconds by default. A loopback marker is preferable for
   physical timing measurements.
4. For stereo/multichannel WAVs set `recording_channel` to the zero-based
   channel carrying the keyboard. A per-case `channel` overrides it. The tool
   refuses to downmix implicitly. Mono files need no channel setting.

Reference files and note schedules must retain their hashes. If the source or
model changes, prepare a new session rather than replacing reference files.
Keep all input WAVs, metadata and reports together for subsequent review.

## Analyze

```sh
/tmp/cz-measure-env/bin/python vcv/czish/tools/calibrate.py analyze /tmp/cz-session
```

This writes `report.md` and `report.json`. Missing takes produce a partial
report; they are not silently skipped. Missing chain metadata, truncated
tails, changed reference/input hashes, ambiguous channel selection, non-finite
samples and possible clipping require attention. Exit code **0** means all
planned files have measurements and no reported quality/metadata issue;
**2** means pending, partial or needs attention; **1** means a command/session
failure. No status means "hardware validated". The machine-readable report
always keeps `hardware_fitted: false`.

Measurements include peak/full-file RMS/DC, leading-silence noise, fitted gain
and waveform residuals against each reference, fundamental pitch for the
stable cosine/saw fixtures, and all eight burst onset intervals. A zero RMS or
residual is encoded as `null` dB rather than nonstandard JSON infinity.
The 24-bit PCM path accounts for SciPy's left-justified int32 representation;
float WAVs are not clipped during loading.

Automatic alignment first compares smoothed amplitude changes and then fits
phase within a 5 ms neighborhood. A periodic tone can align several cycles
apart. Its offset is **not physical latency**; an independent marker or a
manual per-case `offset_seconds` is needed for that claim. A manual offset
maps reference time `t` to recording time `t + offset_seconds`. Quiet or very
slow envelopes may need manual alignment. Review poor correlations and the
source WAVs, even when no quality flag appears.

The burst slope is an interval estimate involving MIDI timing, envelope onset
and the recording clock. It is not an isolated measurement of the DAC's
40 kHz clock. Likewise pitch error can come from keyboard tuning or either
clock. Near-full-scale samples prompt a clipping check, but analog saturation
below digital full scale cannot be detected by that check.

## Fit only after review

The current report fits **gain and phase per take for diagnostics**. Doing
that can conceal errors in level response or transient timing, so the residuals
must not be used to rank converter laws or to claim calibration. They also
contain frontend, envelope and reconstruction errors. Subtracting the raw
chip output does not isolate an analog circuit while these remain uncertain.

Before fitting a model, review the captures and chain, establish a shared gain
reference, then fit converter law, sum/slot behavior, output response, noise
and firmware timing separately. Fit/holdout cases are assigned before
recordings exist; withheld levels, polyphony and pitches must remain unused
during fitting. A fitted result needs a separate holdout comparison with its
measurement conditions and uncertainty. There is no automatic parameter
update in this tool.

## Tool validation

```sh
make -C vcv/czish/tests calibration-check
```

This runs known-signal tests for alignment, gain/DC, pitch, a 400 ppm burst
interval change, packed 24-bit PCM, explicit stereo channels and non-finite
input rejection. It also prepares real silent/cosine/burst/eight-note
references, checks padded MIDI and sample lengths, analyzes delayed copies,
flags clipping and changed inputs, and verifies that absent hardware yields a
pending report. Synthetic copies test the workflow only; they are not hardware
validation. Linux CI runs these checks alongside the existing plugin tests.

Implementation references:
[SciPy WAV input](https://docs.scipy.org/doc/scipy/reference/generated/scipy.io.wavfile.read.html),
[polyphase resampling](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.resample_poly.html),
[MIDI Association SMF specification](https://midi.org/standard-midi-files).
The MIDI reader intentionally accepts the committed fixture format, not
arbitrary user MIDI files.
