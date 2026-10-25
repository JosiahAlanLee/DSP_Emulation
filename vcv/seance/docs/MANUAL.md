# Séance

**Spectral frame core and expanders · SeanceLab · 10 HP core, 4 HP expanders**

Séance holds one short-time Fourier frame in common with the modules to its right. The core analyses its input, hands the frame down the row, each expander changes it in place, and the core resynthesises what comes back. One analysis, one resynthesis, any number of operations between; the operations apply left to right.

[Installation and shared workflow](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md) · [Development](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md)

## First sound

1. Patch audio to **A** on Séance and **OUT** to your mixer.
2. Place one or more expanders directly to the right of Séance, touching, in the order you want them applied. Each expander's **LINK** light comes on; the core's **CHAIN** light shows a row is attached.
3. Turn **WIPE** on a Wipe expander, or press **FREEZE** on a Freeze expander.

With no expanders attached the core is a pass-through with latency. **MIX** blends the dry input, delayed by the same latency, with the result.

Rack audio of ±5 V has 6 dB of headroom before the internal range reaches full scale at ±10 V. Polyphonic inputs are summed.

## How the row works

Only modules touching each other count. The row is: the core, then any Séance expanders to its right without a gap. Another module in the row, or a gap, ends it. Expanders farther right than that are not linked (their LINK light is off) and do nothing.

The frame makes one trip per hop (a quarter of the frame size). Each expander takes its turn when the frame reaches its position; a bypassed expander passes the frame on untouched. The core waits up to 64 samples for the frame to come back, which is more than a row ever needs; if an expander is removed while it holds the frame, the core resynthesises what it has, lights **STALL** briefly, and carries on. The context menu reports the row length, the latency and the number of frames returned late.

The same row can hold two expanders of the same kind, applied twice. The frame is mono; for stereo use two rows.

## Séance (core)

| Control | Range and behavior |
|---|---|
| SIZE | Frame size: 512, 1024, 2048 (default) or 4096 samples. Larger frames resolve pitch more finely and smear time more; smaller ones the reverse. Right-click to type a size; the nearest is chosen. Takes effect at the next frame. |
| SEED | 0–999. Every random decision in the row (Scramble's offsets and diffuser, Freeze's drift) is a pure function of the seed, the frame count since RESET and the bin, so the same seed replays the same texture. |
| MIX | Dry/wet. The dry path is delayed by the latency so the two line up. |
| RESET | Button or trigger input. Frames count from zero again and the frame grid restarts here, so a sequence that fires RESET at its start replays the same frames at the same offsets. Reconstruction continues through a reset. |
| A | The signal that is analysed, processed and resynthesised. |
| B | A reference signal, analysed alongside A for Cross. It is never heard directly. |
| SEED CV | 1 V per 100, added to the knob; the sum wraps at 1000. A Gorgon automation lane can pick the seed per row. |
| MIX CV | 10 V spans the knob. |

Latency is the frame size minus one plus 64 samples: 575, 1087, 2111 or 4159 samples (12, 23, 44 or 87 ms at 48 kHz). It does not change when expanders are added or removed.

Analysis and resynthesis use Hann windows on both sides at a quarter-frame hop and divide by the windows actually summed at each sample, so an unmodified frame reconstructs the input exactly (to float precision) and a restarted grid or a late frame does not break the output.

## Freeze

| Control | Behavior |
|---|---|
| FREEZE | Latch, or gate at **GATE** (1 V). While on, the frame that was current at the moment of freezing is held. Its phases keep turning at the rates measured from the two frames before the freeze (a phase-vocoder frequency estimate), so a held tone stays a tone at its pitch and a held chord stays in motion rather than buzzing at the hop rate. |
| DRIFT | 0–100%: seeded per-bin phase noise on the held frame, up to half a radian a hop. Low values loosen the hold into a shimmer; high values turn it to a cloud. |

Releasing the freeze returns to the live input at once. Changing SIZE while frozen releases it.

## Scramble

| Control | Behavior |
|---|---|
| RANGE | 0–12 semitones: the largest offset, either way, that a spectral peak can be moved. Each peak gets its own offset, a seeded random fraction of RANGE in octaves, so a harmonic input becomes an inharmonic chord of the same partials. Small ranges detune; large ones scatter. **RANGE CV**: 10 V spans the knob. |
| HOLD | 1–64 frames: how long one set of offsets lasts (an epoch). At 1 the offsets change every frame, which is a rough, noisy texture; at 8 (default) or more the partials hold still between changes. |
| PHASE | 0–100%: a diffuser. Adds a constant random phase to each bin for the length of the epoch, which smears transients into a short reverb-like blur. The spread costs level, up to about 3 dB at 100%, since energy pushed past the window is lost. |

A peak is moved with its lobe and re-pitched coherently: the lobe keeps its internal phase structure and advances at the new frequency, so partials land where they are put instead of cancelling between overlapping frames. A held tone keeps its level when moved. Noise is not made of coherent partials and loses a little level when scattered.

## Cross

| Control | Behavior |
|---|---|
| DEPTH | 0–100%. **DEPTH CV**: 10 V spans the knob. |
| SMOOTH | 0–100%: widens B's spectral envelope from single bins to ±127 bins, so B acts as a formant shape rather than a set of lines. |
| MULT / MORPH | MULT (down): A's magnitudes are multiplied by B's normalised envelope raised to DEPTH, then the overall level is restored (up to 24 dB of make-up). With B silent and DEPTH above zero the output is silent. MORPH (up): A's magnitudes move towards B's, geometrically, by DEPTH; at 100% the output has B's magnitudes under A's phases. |

Cross is a vocoder when A is noise or a rich synth and B is a voice or a drum loop; with SMOOTH up it imposes B's formants. The reverse assignment (A the voice, B a chord) gives the voice the chord's lines.

## Wipe

| Control | Behavior |
|---|---|
| WIPE | -100–+100%. Positive: clears everything below an edge that climbs with the knob, a high-pass sweeping up from the lowest bin to Nyquist. Negative: clears everything above an edge that descends as the knob falls, a low-pass sweeping down. 0 is a clean pass. The edge moves on a log-frequency axis, so the sweep is even across octaves. **WIPE CV**: ±10 V spans the knob. |
| SOFT | 0–100%: widens the edge from a brick wall to two octaves. |

## Patch starting points

- **Resampling through the row.** OTIS → Séance (with Freeze and Scramble) → Spoon Bender → CAPTURE → OTIS. Destructive reworking becomes resampling through the patch: each pass through CAPTURE is a new OTIS sample with the row's changes baked in.
- **Gorgon as the hand on the seed.** Gorgon's RESET into Séance's RESET and an automation lane into SEED CV: every row of the score replays its texture exactly, and a different lane value gives a different scatter.
- **Vocoded modular.** Vastish or the modular into A, a drum loop into B, Cross in MULT with SMOOTH around 40%.
- **Two rows for stereo.** Two cores with the same SEED fed the same RESET produce identical textures; different SEEDs give a wide, uncorrelated stereo spread.

## Saving and troubleshooting

Parameters are saved with the patch; the held frame and the chain state are not. Expanders remember nothing across a reload beyond their knobs.

If an expander's LINK light is off, there is a gap or a non-Séance module between it and the core. If STALL flickers constantly, an expander in the row is not being stepped by Rack at all (this should not happen; please report it with the patch).

## Panel marks

The core carries III Boundary (commons or institution): the frame is a commons the row works inside. Freeze carries VI Regulation (returns to set point); Scramble XII Learned invariant (hard walls, one door: one seed opens onto one scatter); Cross IV Centre of decision (authority, contestable: B decides, DEPTH contests); Wipe V Bottleneck (fails the bypass test: bins past the edge do not get through). The marks are the Revision 3 sketching marks of *Clavicula Matricis*, Tabula XIII; see [the glyph README](../../shared/glyphs/README.md).

## Credits

Original algorithms. The phase-vocoder frequency estimate and the phase-locked relocation of peaks follow the standard literature (Flanagan and Golden; Laroche and Dolson); the operations are in the spirit of SuperCollider's `PV_` unit generators and Pure Data's FFT examples, not ports of either.
