# Monomodule Rack

The Elektron Monomachine's six tracks as one Rack module, on **Monomodule**, Shnolk's
chip-level emulation: the Monomachine's own DSP program, run in an emulated DSP56300,
one per track, so it matches the hardware sample for sample. Gorgon plays it through
the sound link, a note and its locks per hit; a TRIG cable plays it from anything else.

**Requires your own Monomachine OS 1.32B.** Elektron offers it as a free download,
[Elektron_SFX6-60_OS1.32B.zip](https://www.elektron.se/wp-content/uploads/2024/09/Elektron_SFX6-60_OS1.32B.zip);
unzip it and choose the `.syx` from the module's menu. The patch saves the file's path,
never a copy. Not affiliated with Elektron, Akai or VCV.

## Play it

1. Add **Monomodule** and choose **Load Monomachine OS 1.32B (.syx)…** in its menu. The
   green light comes on when the six engines are ready, about 3 seconds: loading plays
   every machine once on every track, so no machine's first note can stall the audio.
2. **From Gorgon:** place it immediately to the right of Gorgon 2.4.0 or later and leave
   TRIG unpatched. The blue light shows the link. Gorgon's lanes 1–6 play tracks 1–6:
   each hit is a note, at the pitch of its cell (after the score's key), held while
   Gorgon's gate is high. A machine lock picks the track's machine for that note, and
   P1–P8 locks set its SYN page.
3. **From TRIG:** a polyphonic gate cable, channel *n* = track *n*. A gate is a note,
   at V/OCT's pitch (C4 = 0 V; a mono cable reaches every track), ending when the gate
   falls, when its channel goes away or when the cable is unplugged. A TRIG cable
   overrides the Gorgon link.

Stopping or seeking Gorgon, a RESET edge, a sample-rate change or a dropped block ends
every note, as stopping the hardware does, and changes nothing else: each track keeps
its machine, and an FX track (a REVERB on NEIBOR, say) keeps processing its input.
Loading an OS, as opening a patch does, starts every track on GND.

| Control | |
| --- | --- |
| MACHINE | The machine for notes that don't name one: the 22 machines in the hardware's order. MACHINE CV (0–10 V) overrides it. |
| TEMPO | The engine tempo for synced LFOs and delays. Set it to Gorgon's. |
| GAIN | Output level. |
| P1–P8 + CV | Raw offsets to the SYN page, added to each note's locks or the machine's defaults, sampled at the note. The menu lists the selected machine's parameter names. |
| RESET | A rising edge ends every note. Each track keeps its machine and settings, and an FX track keeps processing its input. Patching Gorgon’s RESET output here ends held notes at every loop wrap; use the sound link, or a gate that fires only on stop/seek, to preserve held notes across loops. |
| INP A / INP B | Audio for the FX machines. |

| Output | |
| --- | --- |
| LEFT / RIGHT | The main pair, OUT BUS AB, 5 V per full scale. |
| TRACKS L / TRACKS R | Each track on its own channel, tracks 1–6, left and right. |

**FX machines** (THRU, REVERB, CHORUS, DYNAMIX, RINGMOD, PHASER, FLANGER) process
audio. Each track's source is under **FX inputs** in the menu, as KIT > ROUTING offers
on the hardware:

| Source | |
| --- | --- |
| NEIBOR | The track before it, as rendered in the same block (track 1 hears silence) |
| INP A, INP B | The INP A or INP B jack, mono |
| INP A+B | Both jacks, INP A left and INP B right |

Track 1 starts on INP A+B and tracks 2–6 on NEIBOR, as in Monomodule Six. The choice
is saved with the patch. INP A and INP B take audio at 5 V per full scale. At any rate
but 44.1 kHz they cross a converter to the engine's rate and back, which the menu's
latency includes.

## Pitch

- **Whole notes only.** V/OCT is rounded to the nearest MIDI note (C4 = 0 V), so CV
  can't play microtones or glides.
- **Tuning is fixed** at the hardware's 440 Hz setting; there is no master-tune control.
  The pitch index is truncated (`(note << 11) / 12`, 2048 units an octave), and the
  master-tune word encodes 440 Hz as −1 unit (the hardware's encoding). Together they
  leave most machines about 0.6–1.2 cents flat; SWAVE SAW and ENS land within half a
  cent.
- **VO-6 rounds its frequency down to a whole hertz.** That happens inside the OS, not in
  Monomodule, so its low notes run flat: about 38 cents at C1, 10–18 cents at C2–G2, and
  about 2 cents or less from C5 up.
- **Octave placements.** At their default settings, FM+ STAT and FM+ PAR sound an octave
  below the played note, and DPRO WAVE has an equally loud partial an octave below it.
  These come from the machines' default FRQ and WAVE settings, not from tuning; a lock on
  those settings moves them.

The cent figures were measured on 2026-10-27 with OS 1.32B, at each machine's default
settings, through Monomodule's own engines.

## Limits of this version

- The AMP, FILT and EFX pages and the LFOs stay at each machine's defaults. Only the
  SYN page is playable, through locks, the knobs and CV.
- The Monomachine has no velocity; a note's velocity only decides whether it plays.
- DPRO DDRW and DENS use Monomodule's stand-in Digibank (the real one lives in the
  +Drive, not in the OS file).

## Timing and cost

Notes are applied on the engine's 16-sample blocks at 44.1 kHz, never early, then
converted to Rack's rate. A gate shorter than one block (about 0.36 ms) still plays,
for one block. The queue to the worker thread is 1024 host samples, plus the
converter's delay; the menu shows the total. An FX machine's DSP works on whole
blocks, so audio through it (INP A/B or NEIBOR) is one more block late, 15–16 engine
samples (about 0.35 ms), beyond the menu's total. On a Ryzen 5 5600X the six engines cost
about a third of one core (0.11 ms of work per 0.36 ms block, at most 0.28 ms), on their
own worker thread.

## Build

```sh
make deps    # Monomodule and its pinned, patched dsp56300 (tools/fetch-core.sh)
make RACK_DIR=/path/to/Rack-SDK test
# Optional real-engine comparison, with your own OS (not a CI fixture):
build/firmware_test /path/to/your/OS1.32B.syx
```

The [platform workflow](../../.github/workflows/monomodule.yml) builds `firmware_test`
but can't run it: CI has no OS image, and `ctest` (and `make test`) leave it out. Run
it by hand with the unzipped OS 1.32B `.syx`. It loads all six engines, then checks,
sample for sample, that a SAW note on track 3 matches a separately driven Monomodule
voice and that THRU on INP A+B matches a direct voice's FX path, and that both are
audible. It prints a line for each check and exits 0 when both pass; with no file it
prints a note and exits 77 (skipped). It passed on 2026-10-27 with OS 1.32B, at
1955fbb.

`make dist` writes the `.vcvplugin` and its corresponding-source archive. The shared
emulated-instrument adapter is `../adapter` (its README draws the AGPL/BSD boundary).

## Changes in 2.0.3

Rack’s **Initialize** returns every track to GND, ends its note (including an FX
track’s continuous trig), and restores FX inputs: INP A+B on track 1, NEIBOR on
tracks 2–6. It keeps the loaded OS and its engines. A held TRIG must fall and rise
again to start a new note. Transport panics and the RESET jack retain their existing
behavior.

Patch FX inputs round-trip in `fxInputs`. Missing entries, a missing or non-array
`fxInputs`, and values that are not integers in 0–3 use that track’s default;
extra entries are ignored. Valid entries in a short or mixed array are preserved.

## License

AGPL-3.0-only (`LICENSE`): Monomodule is AGPL-3.0, dsp56300 GPL-3.0. Notices are in
`docs/`: `MONOMODULE-NOTICE.md`, `DSP56300-LICENSE.md`, `ASMJIT-LICENSE.md`,
`VTUNE-LICENSE.BSD`, `GORGON-SOUND-LICENSE.txt` (the BSD sound-link ABI),
`DEJAVU-LICENSE.txt` (panel labels). Distribute the plugin with its source archive.
