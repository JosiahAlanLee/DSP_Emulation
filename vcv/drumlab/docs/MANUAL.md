# Percussion

> **Retired.** DrumLab Percussion was the first synthesis prototype for Gorgon's drum
> lanes. [Machinemodule](../../machinemodule) is now the drum destination and OTIS the
> sampler (Gorgon system roadmap, item F1). The code stays here for reference and for
> the Gorgon tutorial kit, which still builds it; CI no longer builds or tests it, and
> the rest of this page describes it as it was.

**DrumLab · VCV Rack 2 · 20 HP · up to 16 trigger lanes**

Six original percussion models share eight sound controls and a per-hit control
capture system. The default selector position is **Four-lane kit**: channels
1–4 play Kick, Snare, Hat and FM; the mapping repeats for further channels.
An explicit machine selection plays that model on every trigger lane.

This is a synthesis prototype. It has no sample bank, recording, pattern editor,
kit storage or internal sequencer. Gorgon currently supplies four lanes.

## Connect Gorgon

1. Load [Gorgon-percussion.vcv](../examples/Gorgon-percussion.vcv), or connect
   Gorgon's **GATES**, **PITCH** and **VEL** polyphonic outputs to Percussion's
   **TRIG**, **V/OCT** and **VEL** inputs respectively.
2. Connect Gorgon's RESET to Percussion's RESET for repeatable noise on each
   transport reset and pattern/score wrap. Leave it disconnected if successive
   loops should produce a continuing noise sequence instead.
3. Leave MACHINE at Four-lane kit. Patch LEFT and RIGHT to an audio interface
   or mixer. Select your audio device in Rack's Audio module.
4. In Gorgon, enable the tracker, enter hits and start playback. Use key D with
   C4 notes for each model's base pitch. AIR preserves Gorgon's previously held
   lane pitch; use it after a pitched note when that is the behavior you want.

Gorgon's default 10 V velocity profile matches the default here. If Gorgon uses
its 5 V profile, enable **Velocity full scale is 5 V** in Percussion's context
menu. A connected 0 V velocity is silent; an unpatched VEL input is full level.

Keep Gorgon's gate length below 100% for adjacent hits: a gate must go low
between notes to produce a new rising edge. Default gate length works.

The **HIT** button auditions channel 1 without a sequencer. Controls are captured
at that button edge just as they are at a TRIG edge.

## Models and controls

| MACHINE / voltage | Sound | COLOR | CONTOUR | Base frequency |
| --- | --- | --- | --- | --- |
| Kick / 0 V | Swept sine and a short noise transient | Transient brightness and pitch-sweep time | Pitch-sweep depth | 55 Hz |
| Snare / 1 V | Two body modes and filtered noise | Noise brightness | Body/noise balance | 185 Hz |
| Hat / 2 V | Six inharmonic tones and high-passed noise | Tone spacing and noise brightness | Metal/noise balance | 1,100 Hz |
| FM / 3 V | Percussive two-oscillator FM with feedback | Modulator ratio and feedback | FM index and a small pitch sweep | 130 Hz |
| Membrane / 4 V | Six independently damped resonant modes | Damping, modal detuning and excitation noise | Upper-mode strength | 160 Hz |
| Clap / 5 V | Three noise bursts followed by a tail | Burst spacing and noise brightness | Tail level | Noise; tuning has no effect |

The model numbers describe this original instrument; they are not a mapping of
Elektron machines. Aliasing from high-index FM, clipping and deliberate rate
reduction is part of the sound. These are not claimed to be alias-free models.

| Control | Range and behavior |
| --- | --- |
| TUNE | -48 to +48 semitones relative to the model's base frequency. V/OCT adds an octave per volt. 0 V means the model base, not a C4 fundamental. Gorgon's C4 = 0 V is therefore the reference trigger note. |
| DECAY | 5 ms to 4 s, measured to -60 dB for the main amplitude envelope. Membrane modes have separate damping. Distortion and quantization change the audible decay. |
| COLOR, CONTOUR | 0–100%; model-specific meanings above. |
| DRIVE | Soft saturation. Its gain and output compensation are captured with the hit. |
| BITS | 4–16 bit signed amplitude quantization, captured with the hit. |
| RATE | Holds each output value for 1–64 host samples. At 1 there is no rate reduction. This depends on the host sample rate and does not emulate a particular converter. |

The MACHINE CV input overrides the selector with an absolute model voltage,
rounded to the nearest integer and clamped to 0–5. It does not add to the knob.
All other sound CV adds to the corresponding knob before capture:

| Input | Scale |
| --- | --- |
| V/OCT | 1 V/oct; combined pitch is bounded to +/-4 octaves. |
| DECAY | +1 V doubles decay; -1 V halves it. |
| COLOR, CONTOUR, DRIVE | 10 V spans the whole normalized range. |
| BITS | +1 V adds 1.2 bits, rounded and clamped to 4–16. |
| RATE | +1 V doubles held-sample duration, bounded to 1–64. |
| VEL | 0–10 V by default, or 0–5 V from the menu. |

Mono parameter CV broadcasts across trigger lanes. Polyphonic parameter CV
selects each lane; missing channels contribute zero. The TRIG cable alone sets
the number of lanes, from 1 to 16. A monophonic TRIG fires only channel 1.
Disconnecting or shrinking TRIG clears removed lanes. With no TRIG connected,
HIT can still audition the first lane.

## Hit lifetime, choke and reset

All eight sound knobs and their CV, plus velocity, are read at the rising TRIG
edge. Moving them between hits affects the next hit. It does not alter a current
hit's tuning, filtering, envelope or degradation. This supplies the *instrument*
side of parameter locks; Gorgon's current note cells still contain no sound-lock
data. Patching continuous Gorgon automation here captures its value only when a
trigger arrives, and does not supply automatic kit-default restoration.

Each lane is monophonic. A new hit crossfades from the old hit over 1 ms; the old
hit keeps its captured controls while fading. Extremely fast retriggers inside
that 1 ms can replace an unfinished outgoing fade. Velocity zero does not
retrigger or choke an existing hit.

TRIG and CHOKE detect a rising edge at 2 V and rearm at 0.1 V. CHOKE fades a
lane out over 1 ms. A mono CHOKE broadcasts; a poly CHOKE selects lanes. A choke
and trigger on the same sample gives choke priority. A high CHOKE held after its
edge does not suppress later hits.

RESET clears voices immediately and restarts the deterministic per-lane noise
sequence. It does not invent a hit from an already-held gate. A new TRIG edge
on the same sample as RESET plays after the reset. RESET is mono. Rack's module
reset also restores normal controls and the 10 V velocity profile.

Knobs and the velocity profile persist in Rack patches. Sounding tails and noise
sequence position are transient and are not serialized.

## Outputs and reuse

POLY emits one audio channel per trigger lane, nominally +/-5 V. LEFT/RIGHT
provide an equal-power stereo sum with fixed positions repeating every four
channels: center, center, 45% left, 45% right. The sum is divided by the square
root of the trigger cable's channel count; rests do not change that gain.
It is not a limiter: a dense sum can exceed +/-5 V.

Patch the individual outputs to your own filters, VCAs, mixer sends and effects.
Spoon Bender and Séance can process this audio, and OTIS can record it. There is
no sample-sharing connection on Percussion itself.

## Validation and limits

The portable engine tests cover all models at 44.1/48/96 kHz, a settled kick's
frequency, decay, velocity, quantization and held-sample behavior, independent
outgoing hit settings, repeatable reset, choke and finite extreme inputs. The
real-time engine path is checked for allocations. Real Rack API tests cover
the default kit, polyphony, live control changes, retriggers, channel removal,
velocity profiles, saved settings and reset/choke behavior. Sanitizer targets
cover the portable engine and Rack adapter.

Linux builds and headless Rack API tests have been run locally. The generated
panel artwork is inspected, but the Rack GUI and a live audio device have not
been exercised for this prototype. This is not a sonic comparison against
Machinedrum hardware. The portable audition uses this engine directly.

```sh
make -C vcv/drumlab/tests check RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/drumlab/tests sanitize RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/drumlab/tests gorgon-check RACK_DIR=/absolute/path/to/Rack-SDK GORGON_DIR=/path/to/Gorgon
```

The last check uses Gorgon's actual portable output engine and the real Rack
Percussion adapter with one-sample cables and random processing order. It checks
Gorgon's pitch, velocities, mutes and zero-velocity events at 44.1/48/96 kHz in
both voltage profiles. CI pins that source at Gorgon `cb27eb7`. It does not launch
Studio or test HTTP clock following, audio devices or the Rack GUI.

Regenerate the panel and example with `python vcv/drumlab/tools/make_assets.py`
(fontTools and DejaVu Sans required). Labels are paths so NanoSVG renders them.
