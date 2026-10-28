# CZishLab

CZishLab 2.1 contains **CZish** (30 HP) and **DAC Stage** (16 HP), a BSD Rack 2
plugin. It implements the software v1 feature set of the
[CZ voice](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/CZ_VOICE_SPEC.md)
and [converter](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DAC_STAGE_SPEC.md)
designs. The µPD933 arithmetic and Yamaha word format have reference tests.
**The Casio analog compander, key-follow curves, vibrato/portamento time curves,
and firmware update cadence have not been fitted to a CZ-101.**

No Casio firmware, ROMs, factory tones or manufacturer samples are included.
The example chime and capture tones are original settings.

## Play CZish

Patch polyphonic **V/OCT** and **GATE**, and take **OUT** to a mixer. C4 is 0 V;
mono pitch/gate cables broadcast. A gate above 1 V starts a note; below 0.1 V
releases it. Line Select gives:

| Mode | Notes | Lines |
|---|---|---|
| 1 | 8 | Line 1 |
| 2 | 8 | Line 2 |
| 1 + 1′ | 4 | Two copies of line 1; detune on the second |
| 1 + 2′ | 4 | Line 1 plus detuned line 2 |

Each cable channel owns a note slot. There is no voice stealing. The light by
GATE indicates ignored channels. Changing line mode restarts active notes.
**POLY** is an extension: one independent output channel per note, summing a
pair first in two-line modes and then converting each note separately. OUT
converts the chip's mono sum, so adding POLY externally need not equal OUT.

**EDIT LINE** chooses which line the FIRST, SECOND, WINDOW and two KEY knobs
edit. SECOND's menu includes OFF; otherwise the chip alternates the two waves
on successive cycles. Resonance uses the resonance wave with Saw, Triangle or
Trapezoid window. RING/NOISE works only in paired modes. The detune amount is
NOTE semitones plus FINE/60, applied up or down as selected in the context menu.
OCTAVE affects both lines.

**Envelope editing:**

1. Choose DCO, DCW or DCA for either line, then STEP 1–8.
2. Set RATE and LEVEL in Casio's 0–99 units. The display shows their values,
   the levels by step (not elapsed time), and the published bus word/divider.
3. SUSTAIN chooses a held step or OFF. END chooses the final step, whose
   effective level is always zero. On the END step the display and the LEVEL
   knob show zero, but the stored level is kept: an imported tone still exports
   its own bytes, and the level returns if END moves. Later steps, including a
   sustain beyond END, are ignored.
4. While a key is held, reaching SUSTAIN holds there. Releasing a key starts
   the step after SUSTAIN, including a release before the sustain point has
   been reached. Without a sustain, the envelope is a one-shot. A retrigger
   restarts phase and envelopes from zero.

RATE/LEVEL edits retarget an active segment on the control cadence without
restarting its phase or current level.

The sustain/retrigger policy and the key-follow curves are a firmware-free
implementation, awaiting hardware timing comparison. The documented MIDI
conversion rules supply the envelope rate/level codes; DCO level 66 is +1 octave.
The chip schedules segment completions on its 40 kHz ticks. Vibrato updates
on the provisional 64-tick control cadence.

**DCW DEPTH** scales both DCW envelopes. **DCW CV** adds a per-note offset;
10 V spans the full chip envelope range. The result is clamped before the
chip's high-pitch DCW limit. **VIB CV** adds vibrato depth (10 V spans 0–99).
**BEND** is ±5 V for the selected bend range. The menu holds vibrato waveform
and enable, detune direction, bend range, master tuning and portamento.
As the CZ-101 manual describes, a portamento glide takes the same time whatever
the interval; TIME sets that time (provisional curve: 5 ms at 0, about 4.8 s
at 99). The glide runs only while the gate is high: a V/OCT change made
between notes glides from the last sounding pitch when the next gate arrives.

The **CHIP** options expose raw waves 3 and 7, extra windows, other-voice pitch
modulation and partner cancellation. These are outside the CZ-101 firmware.
The silent wave produces DC, not numerical silence. A window on an ordinary
wave with no resonance partner is also a chip extension. When Resonance
alternates with an ordinary wave, the line's one window shapes both waves'
cycles, as on the chip; export treats that pairing as a CZ-101 tone. All settings are saved in Rack patches.

## Tone files

The menu imports and exports a **single-tone .syx**: 263-byte CZ dump or
264-byte receive-request form. Banks and CZ-1 extended 144-byte tones are
rejected rather than silently truncated. Both nibble orders printed in Casio's
manuals are recognized; an unchanged imported file retains its bytes, channel,
header and nibble order. Newly created dumps use the low-first order explicitly
specified in the CZ-1's CZ-101-compatible format.

Import is undoable. Performance switches, bend, tuning and DAC controls are
Rack settings, not part of the 128-byte hardware tone. CHIP-only combinations
must be disabled before hardware-tone export. Import/export has software
round-trip tests; transfer to a physical keyboard has not been tested.

`examples/original-detuned-chime.syx` is an original two-line ring tone.
The first prototype's port/parameter IDs are preserved. Old attack/release
values migrate into DCA step rates; the old DCW depth and waveform values stay.
The expanded CZ panel takes 30 HP, so leave room around it when opening an old
patch. The old attack/release automation parameters are retained for migration;
new patches automate the individual envelope steps. Migration preserves controls;
the expanded envelope and reconstruction paths change their sound.

## DAC Stage

Patch any audio into **IN** and use **OUT**. Both are polyphonic (up to 16
channels), ±5 V is full scale before DRIVE, and the final output is bounded at
±10 V. Profiles are:

| Profile | Conversion | Rate |
|---|---|---|
| YM FP | Exact ymfm 3-bit exponent / 10-bit mantissa word helpers | Nominal 3.579545 MHz / 64; RATE and CV within ±2 octaves, capped at 192 kHz |
| CZ MODEL | Provisional 12-bit mantissa plus range shift | Fixed 40 kHz |
| LINEAR | 4–16 bits, floor or nearest (ties away from zero) | 1–96 kHz knob; CV can extend to 192 kHz |

RATE is displayed in Hz at the nominal Yamaha clock; changing the menu's chip
clock scales it. RATE CV is 1 V/octave; DRIVE CV is 6 dB/V; BITS CV is 1 bit/V,
rounded and clamped. The readout shows effective Hz and bits. When selecting
LINEAR from the panel selector at the untouched default RATE, it adopts the
current host rate (clamped to the RATE knob’s 1–96 kHz range); leaving
LINEAR with that rate unchanged restores the exact YM default. CZ
selects a 20 kHz TONE default when TONE was still at the previous profile's
default. The selector (left or right click; double-click resets the profile)
applies these defaults on the UI thread as one undo step, including RATE and
TONE. Saved/user-edited settings take precedence. Patch/preset loading and
direct PROFILE parameter writes (including automation and randomization) keep
RATE and TONE unchanged. There is no PROFILE CV input or module context-menu
profile setting. Initialize restores YM FP and all factory parameter defaults.

**AA OFF** uses four-point Hermite input sampling, with one host-sample delay.
**AA ON** uses sinc filtering and a continuously maintained cascade of half-band
2× stages for low device rates. The chosen cascade path changes its delay as
RATE crosses a path boundary; rapid RATE modulation can therefore produce a
transient. Enabling AA starts a fresh input filter. AA is off by default.

**HOLD** uses fractional-timed minimum-phase BLEP steps: it retains hold images
below the host Nyquist and suppresses those above it. **CLEAN** uses a 64-tap
polyphase sinc converter and removes the images. It adds about 31 device samples
of delay. At equal rates, AA+CLEAN adds 62 host samples. Deep AA decimation adds
more: the 48 kHz → 1 kHz input path is about 20 ms. Switching AA, reconstruction,
profile or line mode changes the signal abruptly; do it between notes if needed.

**TONE** is a simple two-pole low-pass, bypassed at maximum. **NOISE** adds
independent, deterministic white noise after decoding, specified as RMS dBFS before reconstruction/tone;
minimum is OFF. It is a chosen noise floor, not a measured instrument model.
Both controls are in CZish's menu too.

The CZ menu selects **Instantaneous** or **Syllabic** companding. The latter
chooses the range with a provisional 1 ms attack / 50 ms release peak detector;
a transient can clip before its range catches up. TRACK adds expansion gain
error per range. Neither candidate is a measured CZ-101 compander, and TRACK=0
is an ideal model rather than a claim about a trimmed keyboard.

## ByteBus

Both modules publish the encoded word's high byte and accept a byte as an XOR
mask **before decoding**. [The bus contract](docs/BYTE_BUS.md) specifies layouts,
clocks and feedback. Noise, reconstruction, tone and gain follow the bus tap.
Flipping sign/exponent/range bits can make jumps or sustained DC.

`examples/cz-yamaha-bytebus.vcv` gates CZish with Fundamental LFO, holds its byte
in Clocked Register, then masks a Yamaha DAC processing CZish's audio. It starts
at −12 dB; select an audio device on Audio 2. Unplug XOR BUS to compare the chain.
ByteBusLab 2.3+ and VCV Fundamental are required.

## Build and validation

```sh
python3 vcv/czish/tools/make_panels.py
python3 vcv/czish/tools/make_example.py
make -C vcv/czish RACK_DIR=/absolute/path/to/Rack-SDK -j2
make -C vcv/czish/tests check RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/czish/tests sanitize RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/czish dist RACK_DIR=/absolute/path/to/Rack-SDK
```

`make -C vcv/czish/tests test` needs no Rack SDK. Tests cover the reference
chip/word arithmetic, line modes, envelopes, tuning, modulation, SysEx, spectral
response and deterministic capture archive. Rack API tests include saved patches,
prototype migration, 16-channel conversion and Register feedback at 44.1/48/96 kHz.
See [validation and provenance](docs/VALIDATION.md) and the
[owner capture procedure](tests/hardware/README.md).
The [recording comparison tools](docs/CALIBRATION.md) prepare padded MIDI,
full-sequence float references and metadata, then measure returned WAVs without
changing the provisional DSP settings. Start with three short takes.

Firmware mode, Gorgon sound locks, tone banks, extra chips and a variable chip
crystal remain the design's deferred extensions. They are not required to play
or edit this version. The panel uses VII Revision for CZish and V Bottleneck for
DAC Stage; meanings are in the shared glyph index. DejaVu Sans's license is in
`docs/DEJAVU-LICENSE.txt`. This project is not affiliated with Casio or Yamaha.
