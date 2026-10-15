# Spoon Bender

**Stereo echo, frequency shifting, reverb and retroactive capture · SpoonBenderLab · 32 HP**

Spoon Bender feeds a stereo echo into a reverberant space. A frequency shifter can treat the echo once or shift every trip around its feedback loop. FREEZE holds the reverb's existing tail. CAPTURE turns a passage you have just heard into a sample for OTIS.

CLEAN processes at Rack's sample rate. ESP adds selected 24-bit node and 16-bit memory quantization at a reduced processing rate. Both modes use original effects algorithms.

[Installation and shared workflow](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/GETTING_STARTED.md) · [Development](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/DEVELOPMENT.md) · [Design specification](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/vcv/spoonbender/SPEC.md)

## First sound

1. Patch mono audio to **IN L**, or stereo audio to **IN L/R**.
2. Patch **OUT L/R** to your mixer or Audio module.
3. Begin with **MIX** at 0%, then raise it to hear the processing.
4. Keep **SHIFT** at zero for ordinary repeats. Change **TIME**, **FEEDBACK** and **DECAY** to establish the echo and space.

IN R is normalled to IN L: an unpatched right input receives the left input. Both inputs and outputs are monophonic ports carrying the two channels of a stereo signal.

Rack audio of ±5 V has 6 dB of headroom before the ESP processing range reaches full scale at ±10 V. Dry audio passes at its incoming level. Multiple summed paths and strong CLEAN feedback can exceed nominal levels.

## Understand the three mixes

| Control | What it blends |
|---|---|
| ECHO MIX | Input audio and delayed audio. This mixture feeds the space stage. |
| SPACE MIX | The echo-stage mixture and the reverb output. |
| MIX | The original input and the result of the stage mixes. |

At ECHO MIX 0%, the space receives the source without echo. At SPACE MIX 0%, the reverb is inaudible. With both stage mixes at 0%, the output is dry at any global MIX.

The direct dry contribution stays at Rack's rate in both engines. ESP's converters add latency to processed audio, approximately 1.7 ms at the default ESP rate with Rack at 48 kHz.

For a mixer send/return, use global MIX 100% and remove dry signal from the stage mix: SPACE MIX 100% gives a reverb return; ECHO MIX 100% with SPACE MIX 0% gives an echo return. Global MIX 100% alone does not guarantee a completely wet output.

## Echo controls

| Control | Range and behavior |
|---|---|
| TIME | 1 ms–2 s, exponential. In ESP budget mode, the actual maximum is shorter; see Engines. |
| FEEDBACK | 0–110%. Higher values send more processed echo back into the delay. Above 100%, repeats can sustain or build until limited. |
| TONE | Left darkens each feedback pass; right removes low frequencies. Center leaves this tone blend neutral. The loop still includes protective filtering and limiting. |
| SHIFT | Bipolar frequency offset, with an exact-zero center deadband. Right-click to enter Hz. |
| FINE / WIDE | FINE spans ±0.1–20 Hz outside the deadband; WIDE spans ±0.1 Hz–2 kHz. Typed values clamp to the selected range. |
| LOOP / POST | LOOP places shifting in the echo feedback path. POST shifts each outgoing echo once. |
| SPREAD | 0%: independent left/right delay paths. 100%: mono input feeds the left delay and feedback crosses sides for ping-pong repeats. |
| ECHO MIX | Blend into the space stage. |

TIME moves glide over about 50 ms, producing a pitch bend as the delay length changes. There is currently no crossfaded-jump option.

The **LIMIT** light indicates high echo-loop level. Feedback limiting bounds the recirculating signal; it is not a master output limiter or an output clipping meter.

### Clock synchronization

Patch timing pulses to **CLOCK**. After two rising edges establish a period, TIME selects a multiple of that period:

**1/4, 1/3, 1/2, 2/3, 3/4, 1, 3/2, 2, 3 or 4.**

The ratios refer to the incoming pulse interval. They are not fixed bar lengths or MIDI clock divisions. For example, with one pulse per quarter note, a ratio of 3/2 gives a dotted-quarter delay.

Right-click TIME to type a ratio such as 3/2 while CLOCK is patched. The closest supported ratio is selected. Without a clock cable, typed TIME values are in seconds.

Clocked times still obey the delay's maximum length. A selected ratio can therefore exceed the available memory; the actual echo time then stops at the limit.

### Frequency shifting

Frequency shifting adds an offset in Hz to the components of a sound. It does not preserve harmonic ratios like ordinary musical transposition.

With a positive LOOP shift, material moves upward by the offset on each feedback pass; a negative value moves it downward. Small values can produce beating or moving coloration. Larger values can produce metallic or inharmonic repeats. Filtering and spectral folding limit how literally repeated offsets behave at the frequency extremes.

At POST, an echo is shifted once on its way out and the shifted output is not used as feedback. Changing LOOP/POST changes how the existing delay content is heard.

## Space controls

The space is an eight-line feedback-delay-network reverb with input diffusion.

| Control | Range and behavior |
|---|---|
| PREDELAY | 0–250 ms before the reverb's diffusion stage. |
| SIZE | Scales the delay network from shorter room-like timing to longer hall-like timing. Changes glide over about 200 ms. |
| DECAY | Nominal RT60 of 0.2–30 s: approximately how long a tail takes to fall by 60 dB under reference conditions. Damping and modulation affect the result. |
| DIFFUSION | Changes input all-pass diffusion, moving from more distinct early echoes toward a smoother onset. |
| DAMPING | Raises high-frequency loss per circulation. Higher values make the tail darker. |
| MOD | Adds slow delay-length movement for a chorused tail. |
| SPACE MIX | Blends the reverb with the echo-stage signal. |
| FREEZE | Holds existing reverb content and stops new input entering its network. The panel latch and external gate are combined; either can hold freeze on. |

While frozen, the network bypasses damping and decay loss, disables modulation and uses integer delay reads. ESP also suspends re-quantization inside the frozen loop so rounding error does not build up in quiet tails. SIZE remains a live control, so moving it can alter the frozen sound.

FREEZE affects the space stage. Dry signal and echoes remain audible according to their mixes. Disengaging freeze lets the tail return to normal decay.

## Engines

| Setting | Behavior |
|---|---|
| CLEAN | Floating-point effects at Rack's sample rate, without deliberate word-length reduction. |
| ESP | Effects at a selectable internal rate, with selected nodes quantized to 24 bits and delay writes to 16-bit resolution. Filters, shifting and conversion use floating point between quantization points. |

ESP draws on number formats documented by MAME's ES5510 implementation. It does not execute Ensoniq effects programs and is not an ES5510 chip emulator. Quantization is part of the sound, especially on repeated feedback and low-level tails.

Choose the internal **ESP rate** in the right-click menu:

| Rate | Maximum echo with the 64K-word echo budget |
|---|---:|
| 29.76 kHz, default | About 1.10 s |
| 44.64 kHz | About 0.73 s |
| 78.125 kHz | About 0.42 s |

Disable **ESP echo memory budget (64K words)** for up to 2 seconds at any ESP rate. The budget covers the two echo lines only; the reverb has its own memory. It is a creative constraint, not a verified EPS-16+ memory specification.

The two lower rates are historically motivated but inferred; the highest is an extended option. See the design specification for the evidence and assumptions.

Switching CLEAN/ESP clears the effects tails. Changing the ESP rate rebuilds the ESP processing path and clears its tail. Changing Rack's sample rate rebuilds the engines and capture history.

## CV inputs and outputs

CV is added to the corresponding knob position and clamped. TIME, DECAY and SHIFT CV act in their controls' mapped ranges, rather than adding seconds or hertz directly.

| Input | Scaling / behavior |
|---|---|
| IN L, IN R | Stereo audio; unpatched IN R receives IN L. |
| TIME | 10 V adds the full knob range. With CLOCK patched, this selects ratios. |
| FDBK | 10 V adds 110 percentage points of feedback. |
| SHIFT | 5 V adds the full positive knob range; −5 V subtracts it. FINE/WIDE determines the Hz mapping. |
| DECAY | 10 V adds the full knob range. |
| MIX | 10 V adds 100 percentage points to global MIX. |
| CLOCK | Rising-edge timing input, high at 1 V and rearmed below 0.1 V. |
| CAPT | Capture trigger, using the same edge thresholds. |
| FRZ | Freeze gate, active at 1 V or above. |

All control inputs use a single channel. Use an external attenuator to reduce modulation depth, especially when patching a ±10 V LFO or Vastish FUN output into SHIFT.

OUT L and OUT R carry stereo audio. Spoon Bender has no sample-position or envelope output.

## Capture the recent past

CAPTURE records what you have already heard, rather than starting a new recording.

1. Let Spoon Bender play for the duration you want to retain.
2. Set **LENGTH** to 1–20 seconds.
3. Press CAPTURE or send a trigger to CAPT.
4. Import the capture into an adjacent OTIS instance.

The captured audio is the mono average of OUT L/R after all mixes, quantized to 16 bits at Rack's sample rate. Audio beyond the capture's full-scale range is clipped. Set MIX to zero to capture the dry input.

Only one capture can await collection at a time. The capture light indicates a pending capture, not that the history has filled. Collection occurs on a UI refresh; the most recently collected capture replaces the previous slot.

After a capture, the new history starts empty. A capture two seconds later contains at most those two seconds even if LENGTH is 20. History also begins empty when the module starts or Rack's sample rate changes.

### Send a capture to OTIS

Place Spoon Bender immediately to the left of OTIS. In OTIS's menu, choose **Import capture from Spoon Bender on the left**, or enable **Follow Spoon Bender captures**.

The Rack UI must be running for collection and import. Importing replaces OTIS's sample and stops its voices. OTIS's memory limit can shorten high-rate captures: at 96 kHz it holds about 10.9 seconds.

If Vastish sits directly right of OTIS with its follow link enabled, it can adopt that completed sample too. Physical adjacency moves sample data; normal audio routing still requires cables.

## Patch starting points

These are proposed starting settings, not supplied presets or hardware comparisons.

| Sound | Start here |
|---|---|
| Short keyboard ambience | SHIFT 0, ECHO MIX 0%, PREDELAY about 20 ms, DECAY about 1 s, SPACE MIX about 20%, global MIX 100%. Adjust SIZE and DAMPING by ear. |
| Echo into a larger space | CLEAN, TIME about 0.3 s, FEEDBACK 35%, SHIFT 0, ECHO MIX 35%, DECAY about 3 s, SPACE MIX 25%, global MIX 100%. |
| Slowly shifting repeats | Start from the echo patch; choose FINE and LOOP, then enter SHIFT +1 Hz. Compare positive and negative shifts. |
| Held reverb texture | Raise SPACE MIX and DECAY, play a chord, then engage FREEZE. Capture only after the tail you want has occupied enough history. |

## Saving and troubleshooting

Rack saves the controls and the ESP rate/budget settings. It does **not** save delay/reverb contents, rolling history or the collected capture slot. Import the capture into OTIS and save that patch to retain its audio.

| Symptom | Action |
|---|---|
| Effect is inaudible | Raise global MIX and the relevant stage mix. A frozen empty space remains silent until unfrozen and excited. |
| Clock ratio produces a shorter delay than expected | Check the 2 s maximum and ESP echo-memory budget. |
| Echo is metallic or out of tune | Check SHIFT and LOOP/POST. Set SHIFT to zero for unshifted repeats. |
| Freeze does not release | Turn the panel latch off and lower the FRZ gate. |
| Capture is shorter than LENGTH | Check time since startup, the last capture or a Rack sample-rate change. |
| Capture disappears after reopening | Import it into OTIS before saving; Spoon Bender's capture slot is not serialized. |

## Panel mark

The mark is **VII — Revision**, from Clavicula Matricis: returns displaced, corresponding here to each shifted repeat coming back moved. It has no control function.

## Credits

BSD-3-Clause. The arithmetic reference is MAME's ES5510 core by Christian Brunschen; Hilbert-transform coefficients use Olli Niemitalo's published design. See [third-party notices](../THIRD_PARTY_NOTICES.md) and [license](../LICENSE).
