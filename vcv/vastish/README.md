# VastishLab for VCV Rack 2

Native C++ port of the eight-voice Pd Vastish synth in `../../pd/vastish`.
Includes all six routing algorithms, fourteen DSP functions, six original
programs, three envelopes, two LFOs, two FUN processors, and all 74 program
parameters. Pd is not needed to run the plugin.

## Install and play

The supplied Linux x64 build was compiled with Rack SDK 2.6.4 on Ubuntu 24.04
and requires glibc 2.35 or newer (Ubuntu 22.04+). It has been tested through the
Rack shared-library API; a live GUI/audio-device session still needs testing.

1. Quit Rack. Copy `VastishLab-2.0.0-lin-x64.vcvplugin` into the `plugins-lin-x64`
   folder inside Rack's user folder. Use **Help → Open user folder** to locate it.
2. Start Rack and add **VastishLab → Vastish**. It occupies 48 HP.
3. Patch pitch and gate from a sequencer or Core MIDI-CV into **V/OCT** and
   **GATE**. For MIDI chords, enable eight-voice polyphony in MIDI-CV.
4. Patch **L/R** to the existing Audio module. The default program is Ladder
   Bass; click its name to load any of the six Pd programs.
5. Click a dark selector to choose a wave, algorithm, DSP type or modulation
   source. Drag knobs; right-click a knob for exact numeric entry. Patch saves
   retain edits. The name shows the last loaded program, even after edits.

## Samples (2.1.0)

The PITCH wave selector has a native-only sixth option, **SAMPLE**, that plays a
WAV file as the oscillator feeding F1–F3, the way the K2000 played samples it
imported (including EPS files) through VAST. Right-click the module and choose
**Load sample WAV…**; the selector switches to SAMPLE and shows the file name.

- 0 V (C4) plays the sample at its recorded pitch; V/OCT transposes from there.
  The sample's own rate is respected, so a 29.76 kHz OTIS export plays in tune.
- Each voice restarts the sample on its gate. Playback is one-shot unless
  **Loop sample** is ticked (whole-sample forward loop).
- Any PCM 8/16/24/32-bit or float WAV loads; stereo is mixed to mono and stored
  as 16-bit, up to 2,097,152 frames (about 44 s at 48 kHz).
- The sample is saved inside the Rack patch, so patches stay self-contained.
- Playback is clean 4-point Hermite interpolation, not ES5505 emulation. An OTIS
  export (or the OTIS link) carries OTIS's *stored* PCM: whatever character its
  16-bit, low-rate capture gave the recording, but not OTIS's playback stage
  (chip transposition, interpolation and filters). To bake that in, record OTIS's
  audible output into a new OTIS take, then use that take.
- Loading or following a new sample swaps it in without a crossfade, so a note
  that is sounding at that moment can click.

Loading, saving and freeing happen on the UI thread; the audio thread only swaps
a pointer and never allocates or waits.

### Following OTIS (2.2.0)

Put Vastish **directly to the right of an OTIS Sampler** and tick **Follow OTIS
on the left** in Vastish's menu. Its SAMPLE wave then tracks OTIS's finished
sample: load a WAV in OTIS, record a new take, or press REWORK, and Vastish
picks up the result on the next screen refresh (the copy is made by the
module's UI, so it needs the Rack window running), labelled `◀ <name>` on the panel. This
is the K2000-reads-EPS-files idea, without the EPROM.

- The link reads OTIS's finished sample memory (16-bit, at OTIS's own rate) and
  plays it through Vastish's clean interpolation, F1–F3 and envelopes. A take
  still being recorded is not shared until it finishes.
- Move OTIS away and the menu says the link is not found; the last sample stays.
  Loading a WAV or clearing the sample in Vastish turns following off.
- The followed sample is saved in the Vastish patch too, so the patch still
  plays if OTIS is absent.

The two plugins talk through a small versioned C interface
(`vcv/shared/SampleShare.hpp`), checked by plugin slug, model slug, a magic
number and a version, so mismatched builds refuse to link rather than crash.

## CV and audio levels

| Port | Meaning |
|---|---|
| V/OCT | 1 V/oct, 0 V = MIDI 60 / C4; up to eight channels |
| GATE | Rack Schmitt gate (high at 1 V, low at 0.1 V); one channel broadcasts |
| VEL | 0–10 V maps to 0–1; latched on gate rise; unpatched = 1 |
| MOD | 0–10 V maps to MWHEEL source 0–1, with 20 ms smoothing |
| L, R | Stereo sum; DSP ±1 maps to ±5 V; no output limiter |
| AMP, ENV2, ENV3 | Per-voice envelopes, 0–10 V |
| LFO1, LFO2 | Per-voice LFOs, usually −10 to +10 V |
| FUN1, FUN2 | Per-voice function output ×10 V; sums may exceed ±10 V |

Mono velocity and MOD inputs broadcast across voices. Pitch remains live while
gate is held, and is held at its last value during release. Removing a poly
channel releases that voice. Retrigger requires a low-to-high gate transition.
A ninth channel is ignored. Rack's source module supplies voice allocation.
Right-click the module and select **Panic / silence held gates** to clear voices;
held gates must go low before they can sound again.

With your working ES-6/ES-7 path, connect the existing Rack Audio input channels
that carry hardware pitch/gate CV to V/OCT/GATE. Connect L/R to your chosen Audio
outputs. Envelope/LFO/FUN outputs can feed the existing ES-3 output channels.
Use the same calibrated CV routing and output scaling you already use in VCV;
the synth does not change device or PipeWire settings. ADAT itself supplies
channels, not a fixed hardware-voltage conversion. No hardware CV/ADAT test was
possible in the build environment.

## Editing

Each of the six columns evaluates

`Adjust + Key*(note−60) + Vel*velocity + Source1*Depth1 + Source2*Depth2`.

Frequency controls use MIDI note numbers, not Hz. Pitch Adjust defaults to 60
with Key=1. Amp Adjust is dB; Pan is −1 left to +1 right. KEYNUM as a modulation
source is `note/127`, exactly as the actual Pd voice implements it. The twelve
sources are OFF, ENV2, ENV3, LFO1, LFO2, FUN1, FUN2, MWHEEL, VEL, KEYNUM, RAND,
and AMPENV. RAND is bipolar and sampled per note.

| DSP type | Adjust (A) | B |
|---|---|---|
| NONE | Unused | Unused |
| LOPASS / HIPASS | Cutoff note | Unused |
| LP2RES / BANDPASS / NOTCH | Cutoff/center note | Q, clamped 0.5–40 |
| LP4 | Cutoff note | Resonance, clamped 0–1 |
| SHAPER | Folding amount, clamped 0.05–16 | Unused |
| DIST | Drive dB | Unused |
| SINE / RINGMOD | Oscillator pitch note | Unused |
| GAIN | Gain dB (−100 = silence) | Unused |
| CRUSH | Bit depth, clamped 1–16, fractional allowed | Unused |
| DECIMATE | Clock-rate note | Unused |
| COMB | Tuning note | Feedback, clamped −0.98…0.98 |

B knobs share a broad numeric range; use exact entry for ladder resonance or
comb feedback. A/D/R are milliseconds; S is 0–1. LFO rate is Hz. FUN A/B select
sources, and EQ selects one of the eight Pd equations. Acyclic FUN dependencies
work in either order. Recursive FUN edges evaluate as zero: the original patch
explicitly does not support FUN feedback.

Algorithms are 1 serial, 2 parallel F1/F2 into F3, 3 F1 into parallel F2/F3,
4 F1×F2 into F3, 5 all three parallel, and 6 F1→F2 plus oscillator→F3. Mixing
coefficients are copied from the Pd generator, including its 0.4/0.5 factors.

## Sound and limits

Naive saw/pulse/triangle, folding, fractional bit crushing and decimation retain
their aliases. LP2RES preserves Pd's imaginary `vcf~` outlet despite its name;
HIPASS is input minus `lop~`; LP4 uses the `bob~` two-substep RK4 solver.
The amp envelope is squared, pan is equal-power, master gain slews over 50 ms,
and stereo output passes through the Pd-style 5 Hz DC blocker.

Only the voices you patch cost CPU. Voices above the incoming polyphony channel
count sleep once their gate is low and their amp envelope has finished (a voice
that is still releasing keeps running). A mono patch of Ladder Bass measured
about 15% of one core before this (2.0.1) and about 2% after; output is
bit-identical, since a finished voice contributes exactly zero.

This is a port of **Vastish**, not an exact Kurzweil VAST implementation or an
EPS/OTIS sampler. It has no sample layers, ROM requirements or dedicated effects
bank. Full-patch bit identity is not promised: Rack gate events use sample timing,
Pd uses block scheduling; noise seeds differ; the comb uses sample-wise cubic
feedback with a 17.00001-sample minimum delay for the Pd 16-sample feedback block rather than Pd's block scheduler. Changing
sample rate clears comb delay memory. Audio gain is preserved, so edited programs
can exceed nominal full scale. See `docs/VALIDATION.md` for measured results.

## Build and test

With a Rack 2 SDK checkout/unpacked directory:

```sh
make RACK_DIR=/absolute/path/to/Rack-SDK -j4
make dist RACK_DIR=/absolute/path/to/Rack-SDK
make -C tests all RACK_DIR=/absolute/path/to/Rack-SDK
make -C tests check
./tests/plugin_test                 # run from this directory
```

If your loader cannot find `libRack.so`, set `LD_LIBRARY_PATH` to the SDK folder
for `plugin_test`. For primitive comparison, obtain the Pure Data **0.55-2** tag
(commit `68a30a3e2672d0d57baff7d862d6b611c79d53ec`) and run:

```sh
make -C tests reference_compare PD_DIR=/absolute/path/to/pure-data
./tests/reference_compare
```

Reimport programs with `python3 tools/import_programs.py` from a complete
DSP_Emulation checkout. Rebuild outlined panel assets with
`python3 tools/make_panel.py` (fontTools + system DejaVu Sans). `docs/panel-preview.svg`
is a generated layout illustration, not a screenshot of Rack.

The source uses standard C++17 and Rack's portable API. Windows/macOS binaries
have not been built or tested.
