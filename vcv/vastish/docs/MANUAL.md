# Vastish

**Eight-voice synthesizer and modulation source · VastishLab · 56 HP**

Vastish puts a waveform or sample through three selectable DSP blocks, followed by amplitude and pan. Six routing algorithms change how the blocks connect. Three envelopes, two LFOs and four mathematical function processors (FUNs) provide per-voice modulation, with outputs for use elsewhere in Rack.

It is an independent interpretation of a VAST-style workflow, developed from the Pure Data Vastish project. It includes six original starting programs. It uses no Kurzweil firmware or sample ROMs.

[Installation and shared workflow](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md) · [Development](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md) · [Validation](VALIDATION.md)

## First sound

1. Patch pitch CV to **V/OCT** and a gate to **GATE**.
2. Patch **L/R** to your mixer or Audio module.
3. Play the initial **Ladder Bass** program. Click its name to select **INIT**, **Ring Bell**, **Fold Pad**, **Crush Lead** or **Comb Pluck**.
4. Click the dark selectors to choose waves, DSP types, algorithms and modulation sources. Drag knobs or right-click them to enter numbers.

Loading a starting program replaces its parameter settings with that program's values. The displayed name is the last loaded program, even after edits. All edits save in the Rack patch.

For a simple editing starting point, choose INIT: a saw waveform with the DSP blocks bypassed. A gate is still needed to open its amplitude envelope.

## Routing and DSP blocks

The PITCH column supplies SAW, PULSE, TRI, COSINE, NOISE or SAMPLE. **PW** changes the pulse width when PULSE is selected.

The algorithm selector above F1–F3 controls their connections. P means the source waveform. These are signal paths; internal mixes use fixed level factors rather than always summing at unity.

| Algorithm | Signal path |
|---|---|
| 1 | P through F1, then F2, then F3. |
| 2 | P through F1 and F2 in parallel; their average feeds F3. |
| 3 | P through F1; its output feeds F2 and F3 in parallel. The final output averages F2 and F3. |
| 4 | P through F1 and F2 in parallel; their product feeds F3. |
| 5 | P through all three blocks in parallel; each contributes at a fixed 0.4 gain. |
| 6 | P through F1 then F2, alongside P through F3. The final output averages F2 and F3. |

Each block has a type selector, an ADJUST value, modulation controls and a B knob. The selected type determines what ADJUST and B mean.

| Type | Function / ADJUST | B |
|---|---|---|
| NONE | Passes the input unchanged. | Unused |
| LOPASS | One-pole low-pass cutoff. | Unused |
| HIPASS | High-pass cutoff. | Unused |
| LP2RES | Pd-derived resonant filter response; cutoff. | Q: 0.5–40 |
| BANDPASS | Band-pass center frequency. | Q: 0.5–40 |
| NOTCH | Notch center frequency. | Q: 0.5–40 |
| LP4 | Nonlinear four-pole low-pass cutoff. | Resonance: 0–1 |
| SHAPER | Sine-shaped nonlinear folding amount: 0.05–16. | Unused |
| DIST | Saturation drive in dB. | Unused |
| SINE | Generates a cosine oscillator, replacing the incoming signal; oscillator pitch. | Unused |
| RINGMOD | Multiplies the input by a cosine oscillator; oscillator pitch. | Unused |
| GAIN | Gain in dB; −100 dB or lower gives silence. | Unused |
| CRUSH | Bit depth: 1–16, including fractional values. | Unused |
| DECIMATE | Sample-and-hold clock frequency. | Unused |
| COMB | Comb-filter tuning. | Feedback: −0.98 to +0.98 |

**Frequency and tuning values are MIDI note numbers, not hertz.** For reference, 60 is about 261.63 Hz and 69 is 440 Hz. PITCH uses semitone units, AMP uses dB, and PAN uses −1 for left to +1 for right.

B has a shared broad entry range, but each DSP type clamps it to its own range. Entering 0.5 for LP4 resonance is more predictable than making a large knob movement.

## Modulation columns

PITCH, F1, F2, F3, AMP and PAN each combine:

~~~text
ADJUST + KEY × (note − 60) + VEL × velocity
       + SOURCE 1 × DEPTH 1 + SOURCE 2 × DEPTH 2
~~~

**KEY** tracks distance from C4 in semitones. **VEL** scales the note's latched velocity. Each SOURCE/DEPTH pair adds a modulation signal in the destination's units.

For example, a PITCH ADJUST of 60 and KEY of 1 follows the incoming pitch normally. To open a filter with ENV2, select ENV2 as a source in that filter's column and raise its DEPTH. Negative depth reverses the direction.

| Source | Meaning |
|---|---|
| OFF | Zero. |
| ENV2, ENV3, AMPENV | The corresponding envelope, 0–1. |
| LFO1, LFO2 | The corresponding LFO, generally −1 to +1. |
| FUN1–FUN4 | Results from the four function processors. Their range depends on the equation and inputs. |
| MWHEEL | MOD input, 0–10 V mapped to 0–1, with a 20 ms transition. |
| VEL | Velocity sampled at gate rise; unpatched velocity is 1. |
| KEYNUM | Current note number divided by 127. This differs from KEY's offset from note 60. |
| RAND | A bipolar random value selected at note onset. |
| AUX1–AUX4 | External CV mapped from ±10 V to ±1 and clamped to that range. |

## Envelopes, LFOs and FUNs

The amplitude envelope, ENV2 and ENV3 have attack, decay and release times in **milliseconds**, and sustain levels from 0 to 1. The amplitude envelope also controls the voice's audio level; its internal gain curve squares the envelope value.

Both LFOs offer COSINE, TRI, SAW, SQUARE and S&H shapes at 0.01–100 Hz. Their phases restart on note onset.

Each FUN selects A, B and an equation:

| Equation | Result |
|---|---|
| A+B | Sum |
| A−B | Difference |
| A*B | Product |
| (A+B)/2 | Average |
| MAX | Larger value |
| MIN | Smaller value |
| ABS(A−B) | Absolute difference |
| A*(B+1)/2 | A multiplied by B remapped from −1…+1 to 0…1 |

A FUN may use another FUN as an input, in either numerical order. Recursive connections do not create a feedback delay: the edge that closes a cycle reads zero. Avoid cycles when you want a predictable mathematical result.

### Patch idea: an LFO shaped by an envelope

Set FUN1 A to LFO1, B to ENV2, and EQ to A*B. Select FUN1 as a modulation source, or patch its output to another module through an attenuator. The envelope controls how much LFO movement reaches the destination.

When sending polyphonic FUN output to a monophonic destination, select or combine channels deliberately. A monophonic input may use only the first channel.

## Ports and polyphony

| Input | Behavior |
|---|---|
| V/OCT | 1 V/oct; 0 V corresponds to MIDI note 60, C4. |
| GATE | Retriggers on a low-to-high transition; high at 1 V, low at 0.1 V. |
| VEL | 0–10 V, clamped; sampled on gate rise. Unpatched is maximum velocity. |
| MOD | 0–10 V, clamped; feeds MWHEEL. |
| AUX 1–4 | Bipolar per-voice control sources. ±10 V gives ±1; mono cables broadcast. |

Pitch and gate channel counts determine the number of voices, up to eight. A mono input broadcasts across the other cable's polyphony. VEL, MOD and AUX do not establish additional voices on their own. Pitch updates while the gate is high and holds its last value during release.

| Output | Behavior |
|---|---|
| L, R | Stereo sum of active voices; internal ±1 corresponds to ±5 V. No output limiter. |
| AMP, ENV2, ENV3 | Per-voice envelopes, 0–10 V. |
| LFO1, LFO2 | Per-voice LFOs, generally −10 to +10 V. |
| FUN1–FUN4 | Per-voice result multiplied by 10 V. Sums can exceed ±10 V. |

The output channel count of the modulation ports follows pitch/gate polyphony, with at least one channel. Use **MASTER** to control the final audio level. Several voices or high-gain DSP settings can exceed nominal audio levels.

## Use a sample

Choose **Load sample WAV…** from the right-click menu. Loading succeeds before the PITCH selector switches to SAMPLE.

- PCM 8/16/24/32-bit and float 32/64-bit WAV files are supported, stored as 16-bit audio.
- A two-channel file loads as a stereo sample. **Load sample WAV as mono…** averages it instead; other channel counts are always averaged to mono.
- Capacity is 2,097,152 frames per channel, about 43.7 seconds at 48 kHz. Longer files are truncated.
- At the normal PITCH settings (ADJUST 60, KEY 1), 0 V plays the recording at its original pitch. There is no automatic tuning analysis.
- A gate restarts the sample. Enable **Loop sample** for a whole-sample forward loop.
- Sample playback uses four-point Hermite interpolation.

A stereo sample plays each channel through its own copy of the three DSP blocks, with the same settings and modulation, so the two sides stay identical in everything but their source. PAN acts as a balance control: centred, each side plays at the level a centred mono sample has on that side. A stereo sample with identical channels sounds exactly like the mono sample. The second set of blocks runs only while a stereo sample is sounding, and it roughly doubles the DSP work for those voices. The PITCH selector shows **ST** before a stereo sample's name.

Loading or clearing a sample disables the OTIS follow link. **Clear sample** removes the sample; it does not select another waveform.

### Follow OTIS

Place Vastish immediately to the right of OTIS and enable **Follow OTIS on the left**. The PITCH wave switches to SAMPLE. Completed recordings, file loads and REWORK results in OTIS update Vastish's source sample. OTIS 2.7 and later share a stereo sample in stereo; an older OTIS shares a mono mix.

The link transfers **the entire stored sample**, with its rate. OTIS's playback window, pitch, filters, clock and reverse setting are not transferred. To include those treatments, record OTIS's audible output as a new take before sharing it.

The Rack UI must be running to copy updates. If OTIS is moved away, Vastish retains its last sample. If it follows an OTIS instance with no sample, SAMPLE becomes silent. Sample replacement has no adoption crossfade and can click during a sounding note.

## Saving and troubleshooting

Rack patches save parameter edits, the loaded sample (both channels of a stereo sample; Vastish before 2.4 opens it with the left channel only), loop setting and follow setting. The sample is embedded, so the original WAV path is not needed. Export audio from OTIS if you also want a separate WAV.

| Symptom | Action |
|---|---|
| No sound | Check GATE, MASTER, the AMP envelope and column gain, and whether SAMPLE has valid audio. |
| A new gate does not retrigger | Let the gate fall before raising it again. After Panic, held gates must also fall first. |
| Unexpectedly large modulation | Check depth units and FUN equations; FUN sums are not confined to ±1. |
| Changing OTIS POSITION does not change Vastish | The link shares sample memory, not OTIS playback settings. |
| Held notes need stopping | Choose **Panic / silence held gates** from the module menu. |

Saw/pulse/triangle waves, shaping, crushing and decimation retain aliasing. The DSP palette follows the Pd implementation and is not a complete hardware VAST implementation. There are no sample layers, manufacturer instrument-file import or onboard effects bank.

## Panel mark

The mark is **X — Generative substrate**, from Clavicula Matricis: the synthesis layer beneath the chain. It is an identifying ornament with no control function.

## Credits

BSD-3-Clause. DSP routines include adaptations from Pure Data by Miller Puckette and contributors. See [third-party notices](../THIRD_PARTY_NOTICES.md) and [license](../LICENSE).
