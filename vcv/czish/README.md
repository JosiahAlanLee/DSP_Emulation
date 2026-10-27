# CZishLab — early CZ and converter experiments

Two **16 HP** Rack modules in one BSD-3-Clause plugin. This is an initial
implementation of the [CZ voice](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/CZ_VOICE_SPEC.md) and
[DAC stage](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DAC_STAGE_SPEC.md) drafts, not a complete CZ-101.
No Casio firmware or factory tones are included.

- **CZish** runs the MAME-derived µPD933 sound path at 40 kHz, with eight
  single-line polyphonic voices. V/OCT, GATE, DCW and DCW CV, wave and window
  choice, and simple attack/release envelopes are playable. The controls map to
  chip registers using an original **provisional** front end.
- **DAC Stage** takes up to 16 channels of audio. It provides YM floating-point
  10.3 word encoding, a **provisional** CZ 12-bit/shift model, and 4–16-bit
  linear quantization. RATE and DRIVE have CV; the CZ rate is fixed at 40 kHz.
  TONE is a simple two-stage low-pass. YM FP defaults to 3.579545 MHz / 64.
- Both modules expose the encoded word to Harriot ByteBus and allow a byte to
  XOR its high bits before the word is decoded. Details are in the
  [ByteBus contract](docs/BYTE_BUS.md).

## First sound

Patch a keyboard, sequencer or Gorgon V/OCT and GATE to **CZish** and patch OUT
to a mixer. DCW changes phase distortion; the envelope controls are approximate
milliseconds, quantized to the chip's available rates. A mono V/OCT or GATE
broadcasts across the other input's channels. The voice is the chip's mono sum;
there is no voice stealing beyond the first eight channels.

For a converter experiment, patch any audio into **DAC Stage IN** and its OUT
to a mixer. Start with YM FP, DRIVE at 0 and CLEAN selected. RATE CV is 1 V/oct;
DRIVE CV is 6 dB/V. ±5 V is full scale before DRIVE. A limiter at ±10 V bounds
the module's audio output.

[`examples/cz-yamaha-bytebus.vcv`](examples/cz-yamaha-bytebus.vcv) uses VCV
Fundamental's LFO to gate CZish, sends its encoded byte through a 1 Hz Clocked
Register to DAC Stage's XOR BUS, and sends CZish audio through the YM stage.
Choose an audio device on the Audio module. The DAC starts at −12 dB because
bitwise changes can make sudden level and DC changes. Unplug XOR BUS to hear the
same chain without word manipulation. ByteBusLab 2.3 or later is needed.

## Reconstruction and current limits

- **CLEAN** uses a 64-tap polyphase sinc output converter, adding roughly 32
  device samples of latency. **HOLD** is an unbandlimited zero-order hold; it
  can produce digital aliases. It is not yet the draft's minimum-phase BLEP.
- DAC Stage's **AA** switch adds a 64-tap input filter (about 32 host samples
  of latency). It improves rejection at moderate rate ratios; the draft's
  half-band cascade for very low device rates is not implemented.
- The CZ model is a guessed instantaneous 12-bit mantissa plus range shift,
  not a measured CZ-101 compander. TRACK changes expansion gain per range step
  as an experiment. It does not claim to reproduce a calibrated hardware unit.
- CZish's front end has one line per voice, simple attack/release envelopes,
  and a mono sum. The CZ's two-line modes, six eight-step envelopes, firmware
  parameter maps, vibrato, key follow, SysEx, polyphonic outputs and Gorgon
  sound locks are not implemented. The chip core includes additional waveform,
  window and modulation behavior for later front-end work.
- BUS OUT is generated **before** CLEAN reconstruction and TONE. Its clock is
  consequently not an audio-output latency reference.

## Build and verify

```sh
python3 vcv/czish/tools/make_panels.py
python3 vcv/czish/tools/make_example.py
make -C vcv/czish RACK_DIR=/absolute/path/to/Rack-SDK -j2
make -C vcv/czish/tests check RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/czish/tests sanitize RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/czish dist RACK_DIR=/absolute/path/to/Rack-SDK
```

`make -C vcv/czish/tests test` needs no Rack SDK. It compares the YM helpers
exhaustively against pinned ymfm, the chip core against the pinned MAME sound
functions on a random register stream, and checks the byte clock, masking and
sinc response. The Rack API test checks both modules, tuning, polyphony and a
real Clocked Register feedback chain at 44.1/48/96 kHz.

The outlined panel lettering uses DejaVu Sans; its license is in
`docs/DEJAVU-LICENSE.txt`. Casio and Yamaha names identify source instruments
and formats; this project is not affiliated with either manufacturer.
