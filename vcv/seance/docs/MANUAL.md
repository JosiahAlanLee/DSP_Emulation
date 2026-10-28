# Séance

**Spectral frame core and expanders · SeanceLab · 10 HP core, 4 HP effects, 8 HP Bridge**

Séance holds one short-time Fourier frame in common with the modules to its right. The core analyses its input, hands the frame down the row, each expander changes it in place, and the core resynthesises what comes back. One analysis, one resynthesis, any number of operations between; the operations apply left to right.

[Installation and shared workflow](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md) · [Development](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md)

## First sound

1. Patch audio to **A** on Séance and **OUT** to your mixer.
2. Place one or more expanders directly to the right of Séance, touching, in the order you want them applied. Each expander's **LINK** light comes on; the core's **CHAIN** light shows a row is attached.
3. Turn **WIPE** on a Wipe expander, or press **FREEZE** on a Freeze expander.

With no expanders attached the core is a pass-through with latency. **MIX** blends the dry input, delayed by the same latency, with the result.

Séance does not clip. It scales ±10 V to ±1 internally and works in floating point, so louder signals pass through intact, and Cross's level make-up can put peaks above ±10 V. Polyphonic inputs are summed.

## How the row works

Only modules touching each other count. The row is: the core, then any Séance expanders to its right without a gap. Another module in the row, or a gap, ends it. Expanders farther right than that are not linked (their LINK light is off) and do nothing.

The frame makes one trip per hop (a quarter of the frame size). Each expander takes its turn when the frame reaches its position; a bypassed expander passes the frame on untouched. The core gives the row a 64-sample deadline. If a gap or removed expander leaves the token waiting, the core reclaims it, resynthesises what it has, lights **STALL** briefly, and carries on. An operation already modifying the frame owns it until it finishes; the core defers reclamation rather than reading a spectrum being changed. A result completed after the deadline also lights **STALL** and may lose output samples. The context menu reports the row length, the latency and the number of frames returned late.

The same row can hold two expanders of the same kind, applied twice. The frame is mono; for stereo use two rows.

## Séance (core)

| Control | Range and behavior |
|---|---|
| SIZE | Frame size: 512, 1024, 2048 (default) or 4096 samples. Larger frames resolve pitch more finely and smear time more; smaller ones the reverse. Right-click to type a size; the nearest is chosen. Takes effect at the next frame. |
| SEED | 0–999. Every random decision in the row (Scramble's offsets and diffuser, Freeze's drift) is a pure function of the seed, the frame count since RESET and the bin, so the same seed replays the same texture. |
| MIX | Dry/wet. The dry path is delayed by the latency so the two line up. |
| RESET | Button, or a trigger at the input (rising through 1 V). Frames count from zero again and the frame grid restarts here, so a sequence that fires RESET at its start replays the same frames at the same offsets. Reconstruction continues through a reset. |
| A | The signal that is analysed, processed and resynthesised. |
| B | A reference signal, analysed alongside A for Cross. It is never heard directly. |
| SEED CV | 1 V per 100, added to the knob; the sum wraps at 1000. A Gorgon automation lane can pick the seed per row. |
| MIX CV | 10 V spans the knob. |

Latency is the frame size minus one plus 64 samples: 575, 1087, 2111 or 4159 samples (12, 23, 44 or 87 ms at 48 kHz). It does not change when expanders are added or removed.

Analysis and resynthesis use Hann windows on both sides at a quarter-frame hop and divide by the windows actually summed at each sample, so an unmodified frame returned within the deadline reconstructs the input exactly (to float precision). A restarted grid uses the actual window sum too; frames returned beyond the deadline can lose output samples.

## Freeze

| Control | Behavior |
|---|---|
| FREEZE | Latch, or gate at **GATE** (1 V). While on, the frame that was current at the moment of freezing is held. Its phases keep turning at the rates measured between that frame and the one before it (a phase-vocoder frequency estimate), so a held tone stays a tone at its pitch and a held chord stays in motion rather than buzzing at the hop rate. |
| DRIFT | 0–100%: seeded per-bin phase noise on the held frame, up to half a radian a hop. Low values loosen the hold into a shimmer; high values turn it to a cloud. |

Releasing the freeze returns to the live input at once.

Changing SIZE drops the held frame. If FREEZE or GATE is still on, the first frame at the new size is held instead. There is no earlier frame at that size to measure from, so its phases turn at the bins' centre frequencies and held pitches snap to the nearest bin (a 450 Hz tone holds at 445 Hz at SIZE 4096). The same happens when FREEZE is already on, or GATE high, as a patch loads, and that first frame holds only the first quarter-frame of input at the window's fading edge: little or nothing (a steady tone holds about 16 dB down). Turn FREEZE off and on once the sound is playing to hold a full frame.

## Scramble

| Control | Behavior |
|---|---|
| RANGE | 0–12 semitones: the largest offset, either way, that a spectral peak can be moved. Each peak gets its own offset, a seeded random fraction of RANGE in octaves, so a harmonic input becomes an inharmonic chord of the same partials. Small ranges detune; large ones scatter. Peaks move by whole bins (the sample rate divided by SIZE: 23 Hz at 2048 and 48 kHz), and an offset of less than half a bin leaves a peak where it is, so low partials move less often and in coarser steps than high ones. At 1 semitone a 220 Hz partial moved for 3 seeds in 40, at the default 3 semitones for about half; a larger SIZE gives finer steps. **RANGE CV**: 10 V spans the knob. |
| HOLD | 1–64 frames: how long one set of offsets lasts (an epoch). The core makes one frame per hop, so 8 frames is 85 ms at SIZE 2048 and 48 kHz. At 1 the offsets change every frame, which is a rough, noisy texture; at 8 (default) or more the partials hold still between changes. |
| PHASE | 0–100%: a diffuser. Adds a constant random phase to each bin for the length of the epoch, which smears transients into a short reverb-like blur. The spread costs level, about 3 dB at 100% with HOLD 8 and about 6 dB at HOLD 1, since energy pushed past the window is lost and, at small HOLD, overlapping frames no longer add up in phase. |

A peak is moved with its lobe and re-pitched coherently: the lobe keeps its internal phase structure and advances at the new frequency, so partials land where they are put instead of cancelling between overlapping frames. A held tone keeps its level when moved. Noise is not made of coherent partials and loses a little level when scattered. A peak's offset belongs to the bin it sits in, so a partial that glides takes a new offset each time it crosses into another bin.

## Cross

| Control | Behavior |
|---|---|
| DEPTH | 0–100%. **DEPTH CV**: 10 V spans the knob. |
| SMOOTH | 0–100%: widens B's spectral envelope from single bins to ±127 bins, so B acts as a formant shape rather than a set of lines. |
| MULT / MORPH | MULT (down): A's magnitudes are multiplied by B's normalised envelope raised to DEPTH, then the overall level is restored (up to 24 dB of make-up). MORPH (up): A's magnitudes move towards B's, geometrically, by DEPTH; at 100% the output has B's envelope (B's magnitudes when SMOOTH is 0) under A's phases. |

With nothing patched to **B** on the core, Cross passes the frame through unchanged.

MULT takes B's shape, not its level. B's envelope is normalised to its peak and A's overall level is restored in every frame, so the output stays at A's loudness however loud or quiet B is, and B's dynamics, such as the gaps between drum hits, do not come through. Only a B frame of exact digital silence mutes A (at any DEPTH above zero). The make-up restores the overall level, not the peaks: in a test, a ±5 V saw shaped by a square peaked above ±10 V. MORPH does carry B's level, more as DEPTH rises, since it moves A's magnitudes towards B's own. With B patched but silent, MORPH fades A out steeply: about 14 dB down at 5% DEPTH, 28 dB at 10%, and inaudible from about 25%.

With A noise or a rich synth and B a voice or a drum loop, MULT gives A the spectral shape of B frame by frame, as a vocoder does, but not B's loudness; with SMOOTH up it imposes B's formants. The reverse assignment (A the voice, B a chord) gives the voice the chord's lines.

## Wipe

| Control | Behavior |
|---|---|
| WIPE | -100–+100%. Positive: clears everything below an edge that climbs with the knob, a high-pass sweeping up from the lowest bin to Nyquist. Negative: clears everything above an edge that descends as the knob falls, a low-pass sweeping down. 0 is a clean pass. The edge moves on a log-frequency axis, so the sweep is even across octaves. It runs from the first bin (the sample rate divided by SIZE) to Nyquist, so the same setting puts the edge lower at larger sizes: at ±50% and 48 kHz it sits near 1500, 1061, 750 or 530 Hz for SIZE 512, 1024, 2048 or 4096. **WIPE CV**: ±10 V spans the knob. |
| SOFT | 0–100%: widens the edge from a brick wall to two octaves. |

## Patch starting points

- **Spectral byte feedback.** Bridge measures activity in eight bands before applying its incoming bit mask. Feed its ACT OUT through Register and Lookup, then return the transformed word to its MASK IN and to OTIS FRAME BUS. [Controls, timing, bands and self-contained example](BYTE_BUS.md). Bridge fits anywhere in the row; it hears the preceding expanders and masks the frame before the following ones.
- **Resampling through the row.** OTIS → Séance (with Freeze and Scramble) → Spoon Bender → CAPTURE → OTIS. Destructive reworking becomes resampling through the patch: each pass through CAPTURE is a new OTIS sample with the row's changes baked in.
- **Gorgon as the hand on the seed.** Gorgon's RESET into Séance's RESET and an automation lane into SEED CV: every row of the score replays its texture exactly, and a different lane value gives a different scatter.
- **Vocoded modular.** Vastish or the modular into A, a drum loop into B, Cross in MULT with SMOOTH around 40%. The loop changes A's timbre hit by hit; A keeps its own level.
- **Two rows for stereo.** Two cores with the same SEED fed the same RESET produce identical textures; different SEEDs give a wide, uncorrelated stereo spread.

## Saving and troubleshooting

Parameters are saved with the patch; the held frame and the chain state are not. A patch saved with FREEZE on holds a new, nearly empty frame as it loads (see Freeze). Expanders remember nothing across a reload beyond their knobs.

If an expander's LINK light is off, there is a gap or a non-Séance module between it and the core. If STALL flickers constantly, an expander in the row is not being stepped by Rack at all (this should not happen; please report it with the patch).

## Panel marks

The core carries III Boundary (commons or institution): the frame is a commons the row works inside. Freeze carries VI Regulation (returns to set point); Scramble XII Learned invariant (hard walls, one door: one seed opens onto one scatter); Cross IV Centre of decision (authority, contestable: B decides, DEPTH contests); Wipe V Bottleneck (fails the bypass test: bins past the edge do not get through). The marks are the Revision 3 sketching marks of *Clavicula Matricis*, Tabula XIII; see [the glyph README](../../shared/glyphs/README.md).

## Credits

Original algorithms. The phase-vocoder frequency estimate and the phase-locked relocation of peaks follow the standard literature (Flanagan and Golden; Laroche and Dolson); the operations are in the spirit of SuperCollider's `PV_` unit generators and Pure Data's FFT examples, not ports of either.
