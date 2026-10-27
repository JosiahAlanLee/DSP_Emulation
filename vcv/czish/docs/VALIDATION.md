# Validation and provenance

This distinguishes source/format tests, software integration and measurements
that still require the owner's keyboard. Software v1 is implemented; a claim of
measured CZ-101 fidelity is not made.

## Primary sources

- MAME µPD933 and ymfm at `55784e9c9fac41cd7b019f23891c92292af9c679`;
  notices and unmodified parity references accompany the plugin source.
- Casio **CZ-101 Operation Manual**, 104A SA. The controls, eight-step
  envelope endpoints, wave combinations, two-line polyphony, detune and key
  follow roles are transcribed from the manual. [Scan](https://manuals.fdiskc.com/flat/Casio%20CZ-101%20Owners%20Manual.pdf),
  SHA-256 `b08f50f82579b21ac8fbe6d980403af0c4bf12a15e362e31aa7175dbe61af041`.
- Casio **Guidebook for MIDI**, November 1985, pp. 19–33. Single-tone framing,
  128-byte data order, rate/level maps and vibrato data tables.
  [Scan](https://synth-diy.org/yg-archives2/raw/CZsynth/files/16_CZ-Docs/13_Casio-CZ-Sysex.pdf),
  SHA-256 `4f78e1d96be8132acab193cee5055e25009016723bafc5a8c009cbbe8fabf43d`.
- Casio **CZ-1 MIDI Specification**, pp. 82–98, explicitly describes the
  CZ-101/CZ-5000 REQUEST 1 compatibility format separately from CZ-1's 144-byte
  tone format. [Scan](https://cz.brusi.com/CASIO-CZ-1-SYSTEM-EXCLUSIVE-SYSEX.pdf),
  SHA-256 `06326c819440408e7a86f66211956ce812a502e3196d111fbe8203c4a771084d`.

The older guide prints high-first nibbles, while the CZ-1 text explicitly
specifies low-first for its compatible format. Import tries both, using valid
one-hot vibrato-wave data and bounded tone fields to recognize the order, and
preserves it on export. A newly authored file uses low-first. The CZ-1 vibrato
depth table appears to print 0E at both 12 and 13; the CZ-101 guide prints 0D at
12, which is the value used. Both print the special 0300 depth value at 99.
No scanned manual text or manufacturer tones are redistributed here.

## Software checks

`make -C vcv/czish/tests check RACK_DIR=...` covers:

- Exhaustive signed 16-bit Yamaha input encoding/round-trip; all 8192 encoded
  words decoded; positive and negative overflow, against pinned ymfm.
- 240,000 randomized register-stream samples against MAME's original sound
  functions, with deterministic identical PRNGs on both sides.
- Linear floor and nearest rounding, Casio range/mask invariants, clock rate
  and compulsory low clock samples, including fast RATE changes.
- Single and paired line allocation, all pair modulation targets, second-wave
  selection, envelope sustain/end, early release and retrigger, live segment edits without phase reset, octave/detune,
  vibrato delay and DCW/key-follow bounds. V/OCT is checked across eight octaves
  to the chip's quantized pitch table tolerance.
- Manual-derived SysEx anchors, 300 randomized tone round-trips in both nibble
  orders, malformed headers/nibbles/truncated input, and byte preservation.
- Rack ports and parameters at 44.1/48/96 kHz, note release, polyphony and
  overflow, saved-tone/source restoration, old-patch migration, effective bit
  CV, and 16-channel low-rate AA conversion.
- A real Clocked Register in a delayed, alternating-order DAC/XOR feedback
  loop at those three host rates.
- All 98 capture tones and their MIDI clips, including note durations, file
  framing, complete note-offs and byte-identical archive regeneration.

ASan/UBSan runs the portable front end and the actual Rack adapters. Leak
checking is disabled in the local restricted host, as for the other plugins.

The spectral tests at 48 kHz report the following settled signals:

| Check | Result |
|---|---|
| AA, 48:1 downsampling, 750 Hz folding to 250 Hz | about 0.800 → 0.0000216 amplitude (over 90 dB rejection) |
| AA, 4:1 and 2:1 downsampling | same rejection threshold passed |
| HOLD at 16 kHz and 16.013 kHz, 997 Hz input | retained lower image within 0.17 dB of analytic ZOH image/fundamental ratio |
| HOLD at 40 kHz and 55.93 kHz | unwanted folded image below −95 dB relative to fundamental in the tested bins |
| 16-bit LINEAR, AA+CLEAN, equal 48 kHz rates, 12 kHz input | −93.76 dBFS RMS error after aligning 62 samples; −88.81 dB relative to the 0.8-peak sine |

The transparency criterion here is **absolute dBFS**, not a guarantee of
−90 dB signal-relative error for all amplitudes. Floor quantization introduces
bias, especially near zero. Spectral tests do not replace listening or prove
analog hardware fidelity.

## GUI, packaging and platform checks

Panels are generated as outlined SVG plus shared widget coordinates. The
static previews are layout aids. The Linux CI job additionally uses Rack's
actual module screenshot mode under Xvfb and uploads the resulting PNGs;
that checks browser widgets without a module instance. It does not exercise
pointer/keyboard interactions. Rack API tests exercise the module data paths.
The example patch is also loaded headless in the development environment.

The workflow builds Linux, Windows, Intel Mac and Apple Silicon packages.
Non-Linux jobs run the portable core/front-end tests; Rack runtime tests and
sanitizers run on Linux. Inspect the PR's checks for the current results.

## Still unmeasured

No CZ-101 recording has been used to fit this version. The instantaneous and
syllabic DAC candidates, output filter, noise level, key-follow curves,
vibrato/portamento curves, retrigger policy and 64-tick firmware cadence remain
provisional. The documented SysEx conversions narrow the unknowns but do not
establish all firmware behavior or analog circuitry. See
[`tests/hardware/README.md`](../tests/hardware/README.md) for repeatable inputs
and recording metadata. No live audio-device or hardware MIDI test was
performed in the development container.
