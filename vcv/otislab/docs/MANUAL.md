# OTIS Sampler

**Sampler, short-loop scanner and sample reworking instrument · OtisLab · 36 HP**

OTIS records or loads a mono sample and plays it through an ES5505-derived integer engine. Pitch, interpolation and chip filters provide its basic playback character. LAB controls add variable clocking, short-window scanning, frame stepping and an original loop-reworking process.

It is an independent instrument inspired by EPS-era sampling. It includes no Ensoniq firmware, instrument library or original ESP effects.

[Installation and shared workflow](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md) · [Development](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md) · [Validation](VALIDATION.md)

## First sound

1. Choose **Load included demo** from OTIS's right-click menu.
2. Patch **L** to a mixer or Audio output.
3. Press **PLAY**. Change **PITCH** to hear transposition.
4. Set **LOOP** to **FWD** and press PLAY again for repeating playback.

L and R carry identical mono audio. OTIS has no internal amplitude envelope. By default, a gate starts a one-shot and releasing the gate does not stop it.

## Record or load a sample

### Record the AUDIO input

1. Patch the sound source to **AUDIO**.
2. Set **REC RATE** and **REC GAIN**. At gain 1, ±5 V fills the 16-bit recording range.
3. Raise the REC switch, or hold the REC input high.
4. Lower both record controls to finish. Press PLAY or send a new gate to hear the new sample.

The switch and REC gate are combined: recording continues if either remains high. A completed take replaces the previous sample and stops its voices. While recording, the previous sample can continue to play. Ordinary recording has no automatic level normalization.

REC RATE is latched at recording start. Changing it during a take affects the next take.

| REC RATE position, left to right | Rate | Maximum duration |
|---|---:|---:|
| 1 | 11,160.714 Hz | 93.93 s |
| 2 | 12,755.102 Hz | 82.19 s |
| 3 | 14,880.952 Hz | 70.45 s |
| 4 | 17,857.143 Hz | 58.71 s |
| 5 | 22,321.429 Hz | 46.96 s |
| 6, default | 29,761.905 Hz | 35.22 s |
| 7 | 44,642.857 Hz | 23.48 s |

Capacity is 1,048,320 mono sample frames. Recording ends automatically when memory fills and FULL lights. Lower the record controls before starting another take.

### Load or export a WAV

Use **Load WAV…** to import PCM 8/16/24/32-bit or float 32/64-bit WAV audio. Channels are averaged to mono, samples become 16-bit, and the file's rate is retained. Long files are truncated to capacity and marked TRUNCATED. File loading does not apply the recording input's capture-rate conversion.

**Export finished sample as WAV…** writes the completed sample memory as a 16-bit mono WAV at its stored rate. Playback settings such as transposition, chip filters and clock changes are not baked into that export.

### Record OTIS's own output

In **Record source**, select **Own output (resample)** or **AUDIO + own output**. The new take passes through the recording path at REC RATE. This can bake playback treatments into a new generation of sample memory.

Set up a sound on the old sample, start recording, and finish the take before replaying it. Repeated resampling can change bandwidth, transients and level. REC GAIN still applies.

## Playback controls

| Control | Behavior |
|---|---|
| PITCH | −48 to +48 semitones, added to V/OCT. Zero offset and 0 V play at recorded pitch. |
| START, END | Select the playback region and loop boundaries, 0–100% of the sample. |
| K1, K2 | Normalized chip-filter coefficients, 0–1. Their meaning depends on the filter topology; these are not cutoff values in Hz. |
| LEVEL | Final output gain, 0–2×. |
| PLAY | Starts or retriggers voice 1. |
| LOOP | OFF: play once; FWD: forward loop; PING: alternating forward/reverse loop. |
| REV | Reverses the starting direction and changes the direction of active voices. |

Choose **Chip filter topology** from the menu:

| Setting | Filter stages |
|---|---|
| 2 low-pass + 2 high-pass | Two K1 low-pass stages followed by two K2 high-pass stages. |
| 3 low-pass + 1 high-pass | Three K1 low-pass stages followed by a K2 high-pass stage. |
| 4 low-pass (K1 / K1 / K2 / K2), default | Two low-pass stages controlled by each coefficient. |
| 4 low-pass (K1 / K1 / K1 / K2) | Three K1 low-pass stages followed by a K2 low-pass stage. |

Enable **Gate release stops playback (default: one-shot)** to stop a voice on gate release. **Declick release/retrigger (1.5 ms fades)** is enabled initially; it softens those boundaries and slightly delays a retrigger. Disable it for hard cuts.

## LAB clock and interpolation

**CLOCK is a playback-rate control, not a tempo-sync input.** CLK CV adds octaves at 1 V/oct.

| Control / menu | Behavior |
|---|---|
| CLOCK | −6 to +3 octaves relative to the chosen chip output clock. Reset to 0 for the baseline path. |
| CLOCK knob moves → Resolution only (pitch held) | Changes playback resolution while compensating pitch, within the pitch-register limits. Default. |
| CLOCK knob moves → Varispeed (pitch follows clock) | Pitch rises or falls with the clock. |
| INTERP | 0–9 effective fractional bits for interpolation, rounded to integer steps. 9 is the chip baseline; 0 gives drop-sample playback. |
| Chip output clock | Selects the baseline output rate and voice limit, shown below. |

| Baseline clock | Maximum Rack voices |
|---|---:|
| 29.76 kHz, default | 16 |
| 44.64 kHz | 14 |
| 78.125 kHz | 8 |

Lowering the voice limit stops excess voices. CLOCK modulation retains the selected mode's voice limit. Effective clock rate is clamped to at least 1 kHz and at most the lower of four times Rack's rate or 400 kHz.

At very low clocks, resolution-only pitch compensation can reach the register's upper limit and go flat. Clock and interpolation settings change playback, not the stored sample. Record the output to retain those changes in sample memory.

## Window scanning and frame stepping

START and END define the region available to the WINDOW section.

| MODE | POSITION and SIZE |
|---|---|
| SEG — Legacy segment | Plays the START/END region. POSITION and SIZE are inactive. Default. |
| SCAN — Fixed-size scan | SIZE sets a window length; POSITION moves that window through the region. |
| FRM — Transwave-style frames | Divides the region into complete SIZE-sample frames. POSITION selects among them. A trailing incomplete frame is omitted. |

SIZE spans 2–65,536 samples on a logarithmic scale. Each volt at SIZE CV doubles the requested sample count. The result is limited to the selected region. Right-click SIZE to enter a sample count.

Set LOOP to FWD or PING for a repeating window. With LOOP OFF, a gate plays the selected window once. Moving a window retains the playback offset within it; it does not restart phase.

POSITION and frame changes are unsmoothed. Fast moves can click or produce zipper textures. Short windows can have a strong pitch determined by their length and playback settings.

### Patch idea: scan a recorded texture

Load a sample with changing timbre. Set LOOP to FWD, MODE to SCAN, and SIZE to 256 samples. Press PLAY and move POSITION slowly. Try a slow external LFO into POS CV, then compare SCAN with FRM.

Start with a small CV range: POS CV adds to the knob, and the combined position clamps at the ends.

## REWORK

REWORK changes the selected window in **stored sample memory**. It uses randomized, overlapping grains and seam smoothing to redistribute the existing sound. It is an original process inspired by sample-reworking workflows.

1. Select the region or window you want to change.
2. Set **COARSE / FINE** and **AMOUNT**.
3. Press **REWORK** or send a rising trigger to **TRIG**.
4. Wait for BUSY to finish before making another request.

COARSE uses longer grains; FINE uses shorter grains. AMOUNT blends the processed result with the source; zero leaves the audio unchanged. The values and voice-1 window are taken when the request is accepted.

Each operation targets voice 1's selected window, but the resulting memory change is heard by every voice reading that region. Audio outside it is preserved. The result attempts to match the region's RMS level, with gain compensation limited to ±12 dB; peaks can still clip.

Repeated operations advance the random seed and can progressively smear transients or darken a texture. Playback continues during processing, and a completed result enters with a short crossfade.

Triggers while BUSY are ignored. **Cancel pending loop rework** cancels pending work; it does not undo a result already applied. Export or save a separate patch before reworking a generation you want to keep. There is no dedicated REWORK undo control.

## Ports and polyphony

Pitch and gate channel counts establish the voice count, subject to the selected clock's limit. A mono cable broadcasts across the other cable's channels.

| Input | Scaling / behavior |
|---|---|
| AUDIO | Mono recording input; ±5 V is full scale at REC GAIN 1. |
| V/OCT | 1 V/oct; 0 V is the sample's original pitch before PITCH offset. |
| GATE | Per-voice play trigger; high at 2 V, low at 0.1 V. |
| REC | Record gate, with the same thresholds; combined with the panel switch. |
| START, END | Per-voice CV; 10 V adds 100% to the knob. Combined values clamp to 0–100%. |
| K1, K2 | Per-voice CV; 10 V adds 1 to the coefficient, clamped to 0–1. |
| VCA | Per-voice level, 0–10 V; 10 V is unity, unpatched is unity. |
| CLK CV | Mono; 1 V/oct added to CLOCK. |
| INT CV | Mono; approximately 1.1 V per interpolation bit. |
| POS CV | Per-voice; 10 V adds 100% to POSITION. |
| SIZE CV | Per-voice; 1 V doubles the window sample count. |
| AMT CV | Mono; 10 V adds 100% to AMOUNT, latched on REWORK. |
| TRIG | Mono REWORK trigger; rising at 2 V, rearmed below 0.1 V. |

Mono control cables broadcast to voices. START, END, K1, K2, POSITION, SIZE and VCA can use independent polyphonic values. They do not establish extra voices by themselves.

VCA defaults to the chip's logarithmic response: a linear voltage ramp gives a strongly curved amplitude change. **VCA CV response → Linear amplitude (via chip volume steps)** changes the mapping for a more conventional external envelope response. Both modes retain the chip's volume steps.

| Output | Behavior |
|---|---|
| L, R | Identical mono voice sum. Internal ±1 maps to ±5 V before LEVEL. Multiple voices may exceed that range. |
| EOC | Combined end/loop-crossing pulse from any voice, 10 V for about 1 ms. Very short loops can keep it high. |
| POS | Position of the first active voice across the whole sample, 0–10 V. Returns to zero when no voice is active. |

## Connect to Spoon Bender and Vastish

Place the modules directly adjacent in the order **Spoon Bender → OTIS → Vastish**.

- Use **Import capture from Spoon Bender on the left** for a one-time import, or enable **Follow Spoon Bender captures** for automatic imports.
- In Vastish, enable **Follow OTIS on the left** to use OTIS's finished sample as its SAMPLE source.

These links need no audio cable, but do require the Rack UI to run. They transfer completed mono sample data and its rate. Spoon Bender imports replace OTIS's sample and stop its voices.

OTIS retains its own playback settings when importing. Its window, filters and clock settings are not sent onward to Vastish. Loading a WAV does not turn off Follow Spoon Bender captures: the next capture can replace that WAV while follow remains enabled.

Spoon Bender captures retain Rack's sample rate. OTIS's fixed capacity therefore holds about 21.8 seconds at 48 kHz or 10.9 seconds at 96 kHz. Longer imports are truncated.

## Saving and troubleshooting

Completed sample audio, its rate and module settings are embedded in the Rack patch. A recording still in progress is not saved as a finished take. Finish recording or reworking before saving the result you want. Pending REWORK jobs and random-seed progression are not restored.

| Symptom | Action |
|---|---|
| Silent playback | Load or record a sample, press PLAY, check LEVEL, K1/K2 and VCA. |
| Playback continues after gate release | Enable gated playback or patch an envelope into VCA. Use **Stop all voices** to stop immediately. |
| A loop clicks while moving POSITION | Unsmooth window moves can do this. Reduce the speed or CV range. |
| REWORK has no effect | Check AMOUNT, sample availability and BUSY. It targets voice 1's current region. |
| Export sounds different from playback | Export writes stored audio. Resample the audible output to include playback treatments. |
| The new sample appears unexpectedly | Check whether Follow Spoon Bender captures is enabled. |

File imports and patch restores can briefly interrupt audio. Completed-sample saving/export does not need to stop playback.

## Panel mark

The mark is **IX — Substitutive change**, from Clavicula Matricis: an old route struck out, corresponding here to REWORK replacing a loop. It has no control function.

## Credits

BSD-3-Clause. Playback arithmetic is adapted from MAME's ES5505/6 implementation by Aaron Giles. See [third-party notices](../THIRD_PARTY_NOTICES.md) and [license](../LICENSE). Numerical validation applies to the documented playback engine; this is not a complete EPS-16+ recreation.
