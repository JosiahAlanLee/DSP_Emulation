# Changelog

## 2.1.1

- CZish: rising DCW segments now ramp all the way to their target. The voice
  chose the envelope direction bit with DCA's level shift, so once a DCW ramp
  passed half its target (or started above half), the chip jumped straight to
  the target. The voice now uses the chip's own target shifts.
- CZish: portamento takes the same time whatever the interval, as the CZ-101
  manual describes; TIME sets the glide time (the curve is still provisional).
  Previously glides ran at a constant rate, so wider intervals took longer.
- CZish: selecting an END step no longer writes zero into its stored level.
  The display and LEVEL knob show zero, and an unedited imported tone exports
  its original bytes.
- DAC Stage: leaving LINEAR with the host rate it chose restores YM FP's exact
  default clock (55,930.39 Hz). A RATE the user set is kept.
- Spec: the variable-ratio converter's cutoff buckets and phase count are
  described as built.
- Recording tooling (previously listed as unreleased; it does not change the
  plugin's DSP or panels, and no hardware fitting is claimed):
  - Offline recording sessions: padded MIDI, exact complete note/chord/burst
    float renders, file/source hashes, chain metadata and fit/holdout assignment.
  - Recording analysis reports peak/RMS/DC/noise, pitch, burst intervals and
    diagnostic aligned waveform errors. Flags missing/changed inputs, incomplete
    tails, possible clipping and ambiguous channels. Reference renders from
    2.1.0 with rising DCW segments differ from 2.1.1's; prepare sessions with
    2.1.1.

## 2.1.0

- CZish: all four line modes, first/second waves, six eight-step envelopes with
  a panel editor, sustain/end, detune, octave, ring/noise modulation, vibrato,
  key follow, bend, tuning and portamento. Added vibrato/bend CV, independent
  polyphonic note outputs and overflow indication. The panel is now 30 HP.
- Single-tone SysEx import/export, original chime and reproducible owner
  capture inputs. No manufacturer tones or firmware are included.
- DAC Stage: minimum-phase fractional HOLD, half-band input AA for low rates,
  instantaneous/syllabic provisional CZ models, independent noise, bit CV,
  nearest rounding, clock menu and effective rate/bit readout; audio bypass.
- Encoded-word ByteBus timing and XOR remain compatible with the prototype.
  Existing parameter/port IDs are preserved; new ones are appended. Prototype
  envelope controls migrate to the step envelopes. Migration preserves
  settings, rather than an identical waveform from the old signal path.
- Added front-end, SysEx, spectral, saved-patch and capture tests; sanitizer
  coverage; actual Rack browser screenshots in Linux CI.
- The Casio analog model and firmware timing/key-follow curves remain
  provisional, with a documented capture protocol. Deferred features include
  firmware mode, Gorgon sound locks, tone banks and a variable chip crystal.

## 2.0.0

Initial single-line eight-voice prototype, three DAC word profiles, CLEAN/raw
HOLD and the encoded-word ByteBus experiment. Superseded by 2.1's reconstruction
and complete firmware-free control interface.
