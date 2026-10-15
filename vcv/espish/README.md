# ESPishLab for VCV Rack 2

**ESPish** is a stereo echo and reverb for the OTIS → Vastish chain. It has an
echo with a frequency shifter inside the feedback loop and an 8-line
feedback-delay-network space. An optional **ESP** engine runs both with
ES5510-style arithmetic: 24-bit nodes, 16-bit delay memory and a reduced
internal rate. A **CAPTURE** button turns the last few seconds of output into
an OTIS sample.

It is **not chip emulation**. No Ensoniq microcode is run or included. The ESP
engine borrows the ES5510's number formats and rounding (as documented in
MAME's core) and applies them to new algorithms. See [SPEC.md](SPEC.md) for
the reasoning and what has and hasn't been verified.

## Install and play

Linux x64 builds come from CI (Rack SDK 2.6.4, Ubuntu 22.04). Windows and macOS
builds are compiled in CI but haven't been run inside Rack.

From a clone of the repo:

```
make -C vcv/espish -j8 RACK_DIR=~/dev/Rack-SDK && make -C vcv/espish install RACK_DIR=~/dev/Rack-SDK
```

Add **ESPishLab → ESPish** (32 HP). Patch a source into **IN L** (IN R is
normalled to IN L) and **OUT L/R** to your mixer or the Audio module. Signals
are ±10 V full scale. At extreme feedback the output can briefly exceed that.

## Controls

**Echo**

| Control | Range |
|---|---|
| TIME | 1 ms – 2 s, exponential. With **CLOCK** patched it selects 1/4, 1/3, 1/2, 2/3, 3/4, 1, 3/2, 2, 3 or 4 × the clock period, measured between rising edges. Time changes glide over 50 ms, which bends pitch like tape. |
| FEEDBACK | 0–110%. Above 100% the in-loop soft limiter holds the level, so repeats sustain without running away. **LIMIT** lights while it is working. |
| TONE | Left darkens each repeat; right thins it. Never boosts. |
| SHIFT | Frequency shift in Hz, exactly 0 at centre (small deadband). **FINE** gives ±0.1–20 Hz and **WIDE** ±0.1 Hz–2 kHz, both exponential. Right-click for exact entry. |
| LOOP / POST | LOOP shifts every repeat again, so repeats climb or fall by a fixed number of Hz (inharmonic, not octave shimmer). POST shifts the echo output once. |
| SPREAD | Parallel stereo (0) to ping-pong (100%). |
| ECHO MIX | Echo dry/wet into the space. |

**Space**

| Control | Range |
|---|---|
| PREDELAY | 0–250 ms |
| SIZE | Room to hall (delay-line lengths, gliding over 200 ms) |
| DECAY | RT60 0.2–30 s |
| DIFFUSION | Discrete early echoes to a smooth onset |
| DAMPING | High-frequency loss per pass |
| MOD | Slow line modulation (chorused tail; less metallic ringing) |
| SPACE MIX | Space dry/wet |
| FREEZE | Button or gate (≥ 1 V). The space's tail holds indefinitely and stops taking new input. The echo and the dry signal still play over it. |

**Global:** MIX (overall dry/wet), ENGINE (CLEAN / ESP), CAPTURE, LENGTH.

**CV inputs** add to the knob: TIME and DECAY ±10 V = full range, FEEDBACK
10 V = 110%, SHIFT ±5 V = full range, MIX ±10 V = full range.

## Engines

**CLEAN**: 32-bit float at Rack's sample rate.

**ESP**: echo and space run at a reduced internal rate behind a band-limited
converter (the same 64-tap polyphase design OTIS uses). Each summing node is
floored to 24 bits. Each delay-memory write is floored to 16 bits, rounding
toward −∞ as MAME's ES5510 core does by default. Filters, the shifter and the
converters run in float between those points. The result is low-level grain
and truncation in long tails, not bit crushing.

Right-click menu:

- **ESP rate**: 29.76 kHz (default; 10 MHz / 336, OTIS's 21-oscillator rate),
  44.64 kHz (10 MHz / 224) or 78.13 kHz (10 MHz / 128, not a period rate).
  The 29.76 kHz and 44.64 kHz rates are inferred from a 1990 review and OTIS's
  clock arithmetic, not confirmed from a service manual.
- **Echo memory**: 64K words (default) or unlimited. 64K words shared by both
  echo lines gives a maximum echo of 1.10 s at 29.76 kHz, 0.73 s at 44.64 kHz
  and 0.42 s at 78.13 kHz. The 64K figure is a deliberate, period-plausible
  limit, not a verified EPS-16 Plus number. Unlimited allows 2 s.

Switching engine or rate clears the effect's tails.

## Capture to OTIS

ESPish always keeps the recent past of its output: mono, (L+R)/2, 16-bit, at
Rack's sample rate, up to 20 s.

1. Place ESPish directly **left** of OTIS: `[ESPish][OTIS][Vastish]`.
2. Set LENGTH (1–20 s). Play, and when something good just happened, press
   **CAPTURE** (or send a trigger to CAPT). The light stays on until the capture
   has been handed over, which takes one UI frame.
3. In OTIS's right-click menu, choose **Import capture from ESPish on the left**,
   or tick **Follow ESPish captures** so every capture loads automatically.
   The sample is named "ESPish capture N" and keeps Rack's sample rate.

After each capture the history starts again. A second capture taken soon after
holds only what was played since the first.

The capture is post-MIX. Turn MIX down to capture the dry input instead.

## Status and tests

Version 1.0.0. Tested headless through Rack's module API. A live GUI and audio
session has not been tested. ENSEMBLE, parallel routing and a space → echo
return are deferred (see SPEC.md).

CI on Linux runs the engine tests, a module test, an AddressSanitizer build and
a cross-plugin test in which the real OTIS code imports from the compiled
ESPish binary. Windows and macOS run the engine tests. Measured values:

| Check | Result |
|---|---|
| ES5510 arithmetic vs MAME's functions | 10M cases match: saturating ADD/SUB (including MAME's SUB quirk at −2²³), wrapping ADDU, 24 → 16-bit write, MAC write-back |
| Shifter unwanted sideband, 100 Hz – 10 kHz | ≥ 44 dB down |
| ESP converter (29.76 kHz) | 1 kHz −0.003 dB, 20 kHz input −89.5 dB, folded image −90.9 dB |
| RT60, set 1 s / 3 s | CLEAN 0.90 / 2.69 s, ESP 0.93 / 2.79 s (±15% required) |
| FREEZE level drift over 30 s | 0.05 dB (both engines) |
| 60 s at every control maxed | Bounded, peak 1.20 (CLEAN) / 0.81 (ESP) full scale. The tail decays once feedback is lowered. |
| CPU, 48 kHz, one core | Roughly 2.5% CLEAN and 3.4–4.9% ESP, depending on the machine. Above the 3% target for ESP. |

## Licence

BSD-3-Clause (see LICENSE). Test code includes functions copied from MAME's
ES5510 core (BSD-3-Clause, Christian Brunschen). Panel text is outlined from
DejaVu Sans (docs/DEJAVU-LICENSE.txt). See THIRD_PARTY_NOTICES.md.
