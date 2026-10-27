# Changelog

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
