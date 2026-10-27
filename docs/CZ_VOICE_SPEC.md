# CZ voice: µPD933 phase distortion for Rack, spec v0.1

Status: **software v1 implemented in CZishLab 2.1; hardware fitting pending.**
The [module manual](../vcv/czish) describes the shipped interface and the
[validation record](../vcv/czish/docs/VALIDATION.md) separates documented MIDI
maps from provisional firmware/analog behavior. The panel is now 30 HP. POLY
outputs one independently converted note (a summed pair in two-line modes).
The initial eight-bit ByteBus XOR/tap extension is also implemented. Deferred
features in §10 remain separate work.

Working names: plugin **CZishLab**,
module **CZish**, continuing the -ish names of Vastish and ESPish (now Spoon
Bender). Names and slugs are the owner's to change.

Hardware claims cite MAME at commit
[`55784e9`](https://github.com/mamedev/mame/tree/55784e9c9fac41cd7b019f23891c92292af9c679)
(2026-10-27) unless marked otherwise. Items marked *provisional* or *unverified*
still need a primary source or a measurement.

The converter half of the sound has its own spec,
[DAC_STAGE_SPEC.md](DAC_STAGE_SPEC.md).

## 1. Purpose

A CZ-101 voice for Rack, built on a port of MAME's µPD933 sound path. It covers:

- the phase-distortion transfer functions, windows, ring and noise
  modulation, and per-voice envelope segments, bit-exact with MAME,
- running at the chip's own 40 kHz,
- a front end that reimplements the CZ-101's lines, eight-step envelopes,
  vibrato, key follow and detune from the instrument's documentation,
- output through the DAC stage's CZ profile.

The aim is a replacement for a noisy CZ-101. The chip also has modes the CZ-101
firmware never used, and the module exposes them as extensions (§5.3).

## 2. Scope

**v1:**

- the core port,
- the firmware-free front end,
- mono output through the DAC stage,
- polyphonic V/OCT and GATE,
- step-by-step envelope editing on the panel,
- `.syx` single-tone import and export,
- the CHIP menu of undocumented modes,
- a DCW CV input.

**Deferred (§10):**

- firmware mode,
- a Harriot BUS input,
- Gorgon sound locks,
- a variable chip clock,
- tone banks,
- more than one chip.

## 3. What MAME establishes

Source files and their SHA-256 hashes:

| File | SHA-256 |
|---|---|
| `src/devices/sound/upd933.cpp` | `fd4a7e4856a594697164b7a58fde0428c451f8ad6eaeb60e70df62e42fb234c3` |
| `src/devices/sound/upd933.h` | `023fdfd3c3ff7eec659669b7cc0c36fdef7b6791c4d4d3421ae9fc25d2726b0f` |

The chip files are BSD-3-Clause, by Devin Acker. The driver, `src/mame/casio/cz101.cpp`, is BSD-3-Clause by Dirk Best and Devin Acker.

### 3.1 Clocking and the instrument

- The µPD933 is clocked at 8.96 MHz / 2 and spends 112 clocks per sample, which
  gives **40 kHz**.
- The CPU is a µPD7811 at 10 MHz, running a 32 KB program ROM. The driver cites
  a 1986 Casio service bulletin listing three revisions: I and II on mask ROM,
  III on EPROM. MAME holds dumps of I and II only.
- MAME needs that ROM to run the whole instrument, along with the µPD7811's
  internal ROM and an initial RAM image. The firmware handles the front panel
  and MIDI, and most likely the envelope sequencing and vibrato too (§3.4).
- The driver's front-panel inputs include:
  - Line Select, Detune and Octave,
  - Vibrato, with on/off,
  - the DCO, DCW and DCA wave, key follow and envelope buttons per line,
  - Env. Point Sustain and End,
  - ring and noise modulation,
  - Portamento on/off and time,
  - Bend Range.
- The keyboard matrix is one bit per key, which fits a keyboard with no
  velocity.

### 3.2 Voices and registers

There are eight voices. Each register write is a register byte followed by a
16-bit value. The low three bits of the register number select the voice,
except for B8–BB.

| Registers | Function |
|---|---|
| 00–07 | **DCA step**: direction, 7-bit rate, sustain flag, 7-bit level |
| 10–17 | **DCO step**: direction, rate, sustain, level unit (2 semitones or 6.25 cents), 6-bit level |
| 20–27 | **DCW step**: as DCA |
| 60–67 | **Pitch**: semitones as 7.9 fixed point |
| 68–6F | **Waveform**: see below |
| 98–9F | **Phase counter**. The CZ-101 zeroes it at note start. |
| B8–BB | **Pitch modulator level**. B8+k writes voice 2k. The next 8-sample latch overwrites it. MAME marks this one "probably". |

The waveform register holds:

- a first wave and an optional second wave, which alternate on successive
  cycles,
- a 3-bit window,
- ring-mod enable,
- pitch-mod enable, with its source set to the other voice or to noise,
- a "mute other" bit.

For an even voice *n*, the modulation bits act on voice *n−2*, modulo 8, so
voice 0's bits act on voice 6.

### 3.3 Waves and windows

**Waves.**

- Documented: saw, square, pulse, double sine, saw-pulse, and resonance.
- Resonance multiplies the phase by the DCW level and hard-syncs to the
  fundamental.
- Undocumented: 3 and 7.
  - **3 is "silent"**, but it outputs −volume/2, a DC level that follows the
    DCA envelope.
  - **7 is labelled a double-frequency pulse** in MAME's comment, but the code
    does something else. Each half-cycle sweeps only the rising half of the
    cosine, over the pivot, then holds low. At DCW 0 that is a rise-and-drop at
    twice the frequency.

**Windows.** The window switch runs for every wave, not only resonance.

- Documented: none, saw, triangle, trapezoid.
- Undocumented: 4 is pulse; 5–7 are double saw.
- Mulcahy says the resonance waves need "additional processing to remove the
  noise". Reading that as these windows is an interpretation; he never uses
  the word.

**Arithmetic.**

- A 2048-entry, 12-bit raised cosine.
- A 512-step logarithmic volume curve, scaling the 12-bit wave to a 13-bit range.
- The effective DCW is limited to `0x400 − min(0x400, step >> 18)`. MAME notes
  this keeps narrow pulses audible and suppresses aliasing.
  - The limit is linear in frequency: about 1024 − 0.2 × f in Hz.
  - It starts to bite at about 39 Hz, where it drops below the envelope's
    maximum of 1016.
  - It is about 934 at A4, and reaches 0 at 5 kHz.

### 3.4 Envelopes

The chip's envelope generator does **one linear segment**:

- It moves toward a target at a rate, where the rate code expands to
  `(8 | low 3 bits) << high 4 bits`.
- It interrupts when a non-sustain segment reaches its target.

The chip has no multi-step envelope and no LFO. From that, *inferred* rather
than stated by MAME:

- The firmware sequences the **eight-step envelopes**, with their sustain and
  end points, by servicing those interrupts.
- The firmware produces vibrato by rewriting the pitch register at its own
  control rate.

### 3.5 Modulation and output

**Pairing.** Voices are processed in the order 5, 0, 7, 2, 1, 4, 3, 6. That pairs
each odd voice (line 1) with an even one (line 2).

**Within a pair:**

- Ring mod multiplies by the partner voice's sample from the same tick,
  divided by 4096.
- "Mute other" subtracts the partner voice, so in the summed output the
  partner cancels. Whether the CZ-101's ring setting also sets mute is not
  established. The front end needs to know (§8).
- Noise pitch modulation latches every 8 samples, at either 0 or +32 semitones.
- Pitch modulation by the other voice reaches about ±7.5 semitones. MAME says
  it is normally unused.

**Tuning.** MAME's pitch table puts A4 at note 62 = 442 Hz.

**Output.**

- The eight voices are summed and clamped to 16 bits.
- The clamp can't be reached from register streams. The maximum DCA index is
  508, which gives a per-voice range of −3884 to +3881, so eight voices peak at
  ±31,072.
- MAME models no DAC, compander or output filter (DAC stage §3.2).

### 3.6 What MAME leaves uncertain

- Interrupt priority, marked TODO.
- Registers B8–BB.
- Everything after the chip.

## 4. Architecture

### 4.1 Core

`Upd933Core` is a portable port of the sound path: the tables, `update`,
`env_t::update` and `update_pitch_step`, with MAME's device and stream layers
removed.

- **API.** `write(reg, value)` and `int16_t render()`, called once per 40 kHz
  sample.
- **Interrupts.** The IRQ timer is replaced by a direct callback when a segment
  completes. The front end runs in the same sample loop, so there's no interrupt
  latency to emulate. How the hardware's interrupt latency affects envelope
  timing is part of what the measurements fit.
- **Noise.** `machine().rand()` becomes a seeded splitmix64, reset with the
  voice. As in Séance, a given seed and gate sequence replays exactly.
- **Parity.** The arithmetic stays bit-exact with MAME for any register stream
  (§7).

### 4.2 Front end

The front end is original code, written from the CZ-101 operation manual and
MIDI implementation (§9). The modes, step counts and ranges below are as
generally documented for the CZ-101. They are to be transcribed from a named
manual edition at implementation time. MAME confirms the controls exist
(§3.1), not their ranges. The front end handles:

- **Line select.** 1, 2, 1+1′ or 1+2′. That gives eight notes on one line, or
  four notes as voice pairs.
- **Tone settings.** Octave, detune, and ring or noise modulation between the
  lines. The line modes that allow modulation are as the manual gives them.
- **Envelopes.** Six eight-step envelopes: DCO, DCW and DCA for each line, with
  sustain and end points. Stages are written as chip segments when each segment
  completes.
- **Vibrato.** Wave, delay, rate and depth, written to the pitch register at the
  control rate.
- **Key follow** for DCW and DCA. How key follow scales levels and rates is
  firmware behaviour, and is fitted (§8).
- **Performance controls** from the manual, such as bend range and portamento.
  These are transcribed at implementation time.
- **Note start.** The phase counter is zeroed at note start, as the CZ-101
  firmware does.

**Parameter maps.** The panel speaks CZ units: rates and levels 0–99, key follow
0–9. These map to chip codes such as 7-bit rates and levels and 6-bit DCO levels.
The firmware's tables are not documented in anything found so far. v1 uses
monotonic maps, labelled *provisional*, and replaces them with tables fitted
from hardware (§8).

**Control rate.** The firmware's update rate is audible as stepped vibrato and
as the timing of envelope stage changes. v1 updates every 64 chip samples
(1.6 ms). This figure is *provisional* until it is measured.

### 4.3 Rate, output and threads

- **Rate.** The core runs at exactly 40 kHz. Its output goes to the DAC stage's
  `DeviceOutput` with the CZ profile, which converts it to the host rate. The
  chip's own aliasing at 40 kHz is kept, and the reconstruction choice is
  HOLD or CLEAN.
- **Mono sum.** The mono output sums the voices, then goes through the DAC
  stage, as MAME sums before output. Whether the hardware compands the sum or
  each voice slot is open (DAC stage §3.2, set (d) in §8). If measurement shows
  per-slot companding, the DAC stage moves before the sum.
- **Threads.** Eight voices at 40 kHz plus a converter is a light load, so
  everything runs on the audio thread. No worker is needed.
- **License.** The plugin includes nothing from `vcv/adapter`. Under the
  adapter README's rule, that is what keeps it BSD-3-Clause like OTIS.

## 5. Rack interface

### 5.1 I/O

**Inputs.**

- **V/OCT** and **GATE**, polyphonic. Each Rack channel is one note slot.
  - Single-line modes take up to 8 channels; two-line modes take 4.
  - Extra channels are ignored, and a light shows the overflow.
  - Slots map to chip voices deterministically, with no voice stealing.
- **Tuning.** 0 V is C4 at A4 = 440 Hz. The front end folds MAME's 442 Hz table
  offset into the pitch register's fine bits.
  - The nominal resolution is 1/512 semitone.
  - The fine table has 12-bit fractions, so adjacent codes differ by up to
    0.44 cent, and a code can sit up to 0.6 cent from the ideal pitch.
  - C4 itself is reachable to within 0.02 cent.
- **DCW CV**, poly or mono. This is an addition beyond the hardware.
  - It offsets the DCW envelope's output, so CV sweeps the phase distortion the
    way a filter CV sweeps a cutoff.
  - Envelope plus CV is clamped to 0–1016, the envelope's own range, before the
    chip's high-pitch limit.
  - Unclamped, a positive offset could reach 0x400 at very low pitches. That
    sets the pivot to 0 and divides by zero. A negative offset would wrap the
    16-bit value to maximum DCW.
  - Parity with MAME holds at CV = 0.
- **VIB CV**: vibrato depth.

**Outputs.**

- **OUT**, mono, through the DAC stage. This is the faithful path, subject to
  the sum-or-slot question in §4.3.
- **POLY OUT**, an extension. Each voice goes through its own DAC stage. It's
  useful for processing voices separately, but it is not how the CZ-101 sounds.
  - "Mute other" cancels only in the sum.
  - On POLY OUT, a muted pair's even channel carries the negated partner.
  - The pair is therefore output as one channel, the sum of the two voices.

### 5.2 Panel (about 24 HP)

| Section | Controls |
|---|---|
| TONE | line select, octave, detune |
| DCO | first and second wave per line |
| ENV | envelope select (DCO/DCW/DCA × line 1/2), STEP 1–8, RATE, LEVEL, SUS, END, display |
| KEY | DCW and DCA key follow |
| VIB | wave, delay, rate, depth |
| MOD | off / ring / noise |
| DAC | RECON (HOLD/CLEAN), TONE, NOISE; the full DAC stage settings are in the menu |

- **Envelope editing.** This mirrors the CZ's own step editing: pick an envelope
  and a step, then set its rate and level. The display shows the step, its
  values and the sustain and end markers.
- **Menu.**
  - Full parameter tables.
  - `.syx` import and export.
  - Tuning.
  - The CHIP menu (§5.3).
- **Panel glyph, proposed: VII Revision**, "returns displaced". The phase is
  bent within each cycle and comes back at its end. VII is already Spoon
  Bender's mark. Sharing one is allowed, as Séance and Séance Bridge share III,
  and the glyph table needs a line either way.

### 5.3 CHIP menu: what the firmware never used

Each item is labelled "outside the CZ-101 firmware":

- Undocumented waves 3 and 7, as the code renders them (§3.3).
- Undocumented windows: pulse, and double saw.
- Windows applied to any wave. The chip applies them regardless of wave. That
  the firmware only pairs them with resonance is *unverified*. The only support
  is Mulcahy naming Resonance I–III after saw, triangle and trapezoid.
- Pitch modulation by the other voice of a pair.
- The "mute other" negation used on its own.

### 5.4 Tones

- The current tone is saved in the patch.
- A `.syx` single-tone dump imports and exports in the CZ-101's SysEx format.
  The format is transcribed from Casio's MIDI implementation documentation at
  implementation time.
- Only the owner's own tones are used. No Casio factory tones are bundled.

## 6. Shared pieces

- **`vcv/shared/DacStage.hpp`**: `DeviceOutput` and the CZ profile (DAC
  stage §6).
- **`vcv/shared/RateConverter.hpp`**: the fixed-ratio converter, 40 kHz to
  host (DAC stage §6).
- **`vcv/shared/PanelWidgets.hpp`** and the panel generator, per
  [PANELS.md](PANELS.md).
- **v1.1:** `ByteBus.hpp` and `GorgonSound.hpp` (§10, item 7).

## 7. Testing

**Core parity.** A reference harness compiles excerpts of MAME's `upd933.cpp`
at the pinned commit. A thin stub stands in for MAME's device and stream
classes. This extends the excerpt approach Spoon Bender uses for the ES5510
helpers, which copies plain functions with no stub. The µPD933 code lives in
member functions of `upd933_device`, so it needs the stub. Scripted register
streams are compared sample for sample, int16 for int16, and cover:

- every wave and window with DCW swept from 0 to the limit,
- second-wave alternation,
- the DCA, DCW and DCO segments, up and down, with sustain and interrupt,
- DCO level units,
- ring mod, mute and other-voice pitch modulation,
- noise modulation, with both sides fed the same injected PRNG,
- the high-pitch DCW limit,
- pitch-register extremes,
- the summed peak with all eight voices at full level: −31,072, confirming the
  clamp is never reached,
- DCW CV at 0 and at its clamp limits, including below 39 Hz.

**Front end.**

- Envelope timelines: sustain, end, a retrigger during release, and a release
  before sustain.
- Line modes and channel limits.
- Detune and octave.
- Vibrato delay.
- `.syx` round trip.

**Tuning.**

- 0 V gives 261.63 Hz within 0.05 cent.
- V/OCT tracks within 0.6 cent across the chip's pitch range, and matches
  MAME's pitch tables exactly.

**Rack module.** Random module order, at 44.1, 48 and 96 kHz, with poly counts
1, 4 and 8, under ASan/UBSan.

**Hardware.** Hardware comparison is §8. Results go in the plugin's
`docs/VALIDATION.md`, stating the recording chain, settings and sample rate.

## 8. Measurement plan (the owner's CZ-101)

This plan fits three things: the DAC stage's CZ profile, the front end's
parameter maps, and the control rate.

0. **Trim first.** Do the DAC offset adjustment. The sources are Mulcahy for the
   CZ-1, and the Zen Instruments CZ-1000 page, which wasn't fetched for this
   draft (DAC stage §3.2). If the untrimmed grit is worth keeping, record sets
   (b) and (d) below before trimming. They become the reference for TRACK.
1. **Chain.**
   - CZ-101 line out into the Digi 003 line input, at the highest rate the
     interface runs, 24-bit.
   - Notes come from a MIDI sequence for repeatable timing. Tones arrive as
     SysEx.
   - Every tone and sequence used is committed under `tests/hardware/`.
2. **Sets.**
   - (a) **Silence:** the noise floor and the hum spectrum.
   - (b) **One note, saw wave, window none, DCW at 0** (a pure cosine), with DCA
     sustain at four levels. This fits the compander law: distortion and noise
     against level.
   - (c) **10 ms tone bursts.** Does the noise floor lag the envelope? If it
     does, the law is syllabic; if not, instantaneous.
   - (d) **1, 2, 4 and 8 notes** of one tone. This tests companding on the sum
     against companding per voice slot.
   - (e) **A saw at full DCW, one note per octave.** This measures the output
     filter and the high-pitch DCW limit.
   - (f) **Rate and level ladders** for DCA, DCW and DCO, about ten points each
     across 0–99. These fit the firmware maps.
   - (g) **Vibrato** at several rates and depths. This fits the control rate.
   - (h) **Key follow** 0, 5 and 9 at three pitches.
   - (i) **Ring modulation in a pair**, recorded alone and against the partner
     line alone. This settles whether ring also sets "mute other".
3. **Analysis.**
   - Render the same notes through the core with the DAC stage bypassed.
   - The difference from the recording isolates the analog path.
   - Fit the compander model, the TONE response and the noise floor from it.
   - Fit the maps from (f)–(h).
4. **Optional reference.** A dump of the owner's own CZ-101 ROMs, run in MAME
   locally with µPD933 writes logged for each test tone, would show the
   firmware's maps exactly.
   - MAME needs the program ROM, the µPD7811's internal ROM and the initial RAM
     image.
   - MAME has no entry for Version III.
   - The ROMs and logs stay out of the repository.
   - Tables taken from firmware logs sit awkwardly with §9 and the README's
     statement that nothing is derived from manufacturer firmware. So by
     default the logs are for validation only, and the shipped tables come from
     hardware recordings. Changing that is the owner's call (§10, item 3).

## 9. Licensing and provenance

- **License.** The new plugin is BSD-3-Clause.
- **`THIRD_PARTY_NOTICES.md`** covers:
  - the µPD933 port (BSD-3-Clause, Devin Acker), with the hashes above,
  - the floating-point helpers through the DAC stage (ymfm, BSD-3-Clause,
    Aaron Giles),
  - the converter design shared with OTIS.
- **Not included:** Casio firmware, ROM images and factory tones. The front end
  is written from the operation manual and the MIDI implementation, not from a
  firmware disassembly.
- **Trademarks.** Casio and CZ are Casio's trademarks. The emulation is
  independent and not affiliated with Casio, in the same form as the README's
  existing notice.

## 10. Open decisions

1. **Names, slugs and glyph.** CZish / CZishLab, and VII Revision, are
   placeholders.
2. **Firmware-free front end (proposed) or firmware mode.**
   - MAME's µPD7810 CPU core is BSD-3-Clause (Juergen Buchmueller), so a
     firmware mode with a user-supplied ROM, in the Machinemodule pattern, is
     license-compatible.
   - The cost is emulating the CPU, including its on-chip serial port, which
     carries MIDI. The key matrix, LCD and cartridge come with it; MAME already
     has BSD-3-Clause models of the HD44780 and the RA-3. The front-panel
     parameters would also only be reachable through SysEx or emulated buttons.
   - Proposal: v1 without firmware. Revisit if the fitted maps miss.
3. **Parameter maps.**
   - Ship the provisional maps labelled, or wait for §8.
   - Whether firmware-logged tables (§8, step 4) may ship at all, which would
     change the README's provenance statement.
4. **Additions beyond the hardware in v1.** DCW CV, the CHIP menu and POLY OUT.
   Proposal: all three, labelled.
5. **One chip, or more.** One chip is 8 voices, or 4 with two lines. Proposal:
   one chip in v1. More chips is an extension, and is not CZ-101 behaviour.
6. **Velocity.** The CZ-101's keyboard has none (§3.1). Proposal: none in v1.
   DCW CV can take a velocity cable.
7. **v1.1 candidates.**
   - **A Harriot BUS input** that writes waveform-register fields (first wave,
     second wave, second-wave enable) per note at gate time. The bit assignment
     is still to be decided.
   - **Gorgon sound locks.** Machinemodule's receiver is AGPL adapter code, so
     these would be written fresh on `vcv/shared/GorgonSound.hpp`.
   - **A variable chip clock**, like OTIS's. It would move both the pitch table
     and the 40 kHz rate together, as changing the crystal would. It needs the
     variable-ratio converter (DAC stage §6).
   - **Tone banks.**
