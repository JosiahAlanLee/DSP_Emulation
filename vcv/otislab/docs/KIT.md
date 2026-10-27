# Otis Kit

**OtisLab 2.11 · 18 HP · a bank of eight sample slots on OTIS's playback arithmetic**

Otis Kit is the playback half of the "Otis flavor" in Gorgon's
[DRUM_MACHINE.md](https://github.com/JosiahAlanLee/Gorgon/blob/main/docs/DRUM_MACHINE.md):
a bounded bank, one slot per hit, on the same ES5505-derived playback arithmetic
as OTIS Sampler. Its capture companion, [Otis Capture](CAPTURE.md), records
takes into a slot directly on its right.

It reuses OTIS rather than multiplying it, as that document asks: there is one
sample memory, one chip clock and one output converter, and the eight slots are
regions of that memory, as the EPS laid out a bank. Slot *n* plays on chip voice
*n* through OTIS's own voice code: interpolation, the K1/K2 filters, the 8-bit
log volume, declick and the host-rate converter are unchanged.

## Slots and memory

- The bank holds eight slots, packed in order into OTIS's sample memory: about
  23.8 s in all at the bank's nominal 44.1 kHz. Slots are mono; a two-channel
  file is averaged.
- A slot keeps its own sample rate. Instead of resampling, its rate becomes a
  pitch offset, `log2(slot rate / 44100)` octaves, so it plays at its recorded
  pitch at TUNE 0.
- When the memory fills, the slot that does not fit is truncated (the menu says
  so) and the slots after it are empty. Loading never moves earlier slots.
- Loading or clearing a slot rebuilds the bank and stops any sounding hits.
  Loading is something done between takes.
- Takes from Otis Capture are written into free memory instead, and replace
  their slot only when complete; a hit already sounding keeps the take it
  started with. See [Where takes go](CAPTURE.md#where-takes-go).

## Controls and ports

Each of the eight rows is one slot:

| Control | Behavior |
|---|---|
| TUNE | −24 to +24 semitones. |
| DECAY | The time to fall 60 dB, from 20 ms (fully counterclockwise) to 10 s, exponential in the knob. Fully clockwise holds: no decay, the slot plays to its end. A decaying hit stops once it is 80 dB down. |
| LEVEL | 0–100%, through OTIS's chip volume register (100% is unity). |
| FILTER | K1, the cutoff of OTIS's filter pair, 0–100%. K2 stays open. |
| START | Where in the slot playback starts, 0–100% of the slot. |
| REV | Plays the slot backwards from its end, down to START. |

A light by each slot number flashes on its hits.

| Port | Behavior |
|---|---|
| GATE | Hits, polyphonic up to 16 channels; a rising edge (2 V, falling at 0.1 V) is a hit. With SLOT unpatched, channel *n* plays slot *n* (channels 9–16 play slots 1–8 again), so an eight-channel gate cable is eight drum lanes. |
| SLOT | A byte bus. When patched, bits 0–2 of its word choose the slot for every hit, read on the hit's own sample; a clocked SLOT uses its latched word. This is how OTIS's PATCH input chooses a patch. |
| PITCH | 1 V/oct, polyphonic. The hit's own channel is read and latched with the hit, added to TUNE. |
| LEVEL | 0–10 V, polyphonic, latched per hit like PITCH and multiplied with the slot's LEVEL. Unpatched it is full. |
| OUT | The bank's mono output, ±5 V full scale. |

A slot is one voice: a new hit on a slot that is still sounding fades it out
(OTIS's 1.5 ms declick) and restarts it, as a drum machine's track does, while
different slots overlap freely. An empty slot ignores its hits. Hits are
one-shots; a falling gate does not stop them.

TUNE, LEVEL, FILTER and DECAY act on a sounding hit as they move. START and REV
are read when a hit starts. PITCH and LEVEL CV are held from the hit.

## Context menu

- **Load a folder of WAVs into slots 1–8…**: the folder's WAV files in name
  order fill slots 1 to *n* (at most eight); other slots keep what they had.
  F2's renders, written beside the breaks model with their manifests, load this
  way.
- One submenu per slot, showing its file, length and any truncation: **Load
  WAV…** and **Clear**.
- **Chip clock**: 29.8 kHz (the default, OTIS's 16-voice clock), 44.6 kHz or
  78.1 kHz. All of them have at least eight voices.
- **Drop-sample playback**: turns the chip's interpolation off, for a rougher
  sound.
- How much of the bank is in use, and **Compact the bank**, which packs every
  slot, takes included, back into one run of memory (it stops sounding hits).

## Saving and reset

The patch saves the eight slots' audio (16-bit PCM, as OTIS saves its sample),
names and rates, the chip clock and the drop-sample setting, so a kit reopens
exactly as it was. Takes from Otis Capture are saved the same way, copied out of
memory without stopping the audio thread. An empty slot is saved as `null`. A slot whose saved rate
is invalid or whose audio is damaged is left empty, with the error in the menu,
and the other slots still load. Rack's Initialize empties the bank and restores
the controls.

## Validation

- `tests/kit_test.cpp`: packing, contiguity and truncation when the memory
  fills; every slot, played from the packed bank, is bit-identical to OTIS
  playing that slot's sample alone, forwards and in reverse, from a START,
  transposed, filtered, at a lower level, at each chip clock and at two host
  rates; a slot at another rate gets the same chip increment as its own rate
  would; DECAY falls 60 dB in its time and stops, and is bit-identical to OTIS
  when off; slot choice from SLOT or the channel.
- `tests/kit_module_test.cpp`, through the Rack module: ports; an empty bank
  silent; channel *n* to slot *n* with empty slots ignored; SLOT bits on the
  hit's sample and a clocked SLOT's latched word; PITCH and LEVEL latched from
  the hit's channel, with TUNE, START and REV; a saved bank restored into a
  fresh module that plays bit-identically, with the clock and drop-sample
  settings; folders loaded in name order, two-channel files averaged; invalid
  or damaged saved slots; Initialize. The ASan/UBSan build runs it too.
- `tests/plugin_test.cpp`: the compiled plugin creates Otis Kit and plays a
  restored bank.
- OTIS Sampler's legacy trace is unchanged: the engine's new per-voice region
  and decay are off unless a kit sets them.
