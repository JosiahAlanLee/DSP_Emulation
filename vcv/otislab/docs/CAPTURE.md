# Otis Capture

**OtisLab 2.11 · 8 HP · takes of a set number of beats, into a slot of Otis Kit**

Otis Capture is the capture companion of [Otis Kit](KIT.md), the second half of
the "Otis flavor" in Gorgon's
[DRUM_MACHINE.md](https://github.com/JosiahAlanLee/Gorgon/blob/main/docs/DRUM_MACHINE.md).
Place it directly to the right of an Otis Kit. It records an external input,
the kit's own output, or both, for a set number of beats, into an explicit slot.
The new take replaces the slot only when it is complete, and a hit already
sounding keeps the take it started with.

## Using it

1. Place Capture directly right of Otis Kit; its display shows **READY** and the
   room in the bank. On its own it shows **NO KIT** and does nothing.
2. Choose **SLOT**, **LENGTH** (1/4 beat to 32 beats) and **SOURCE**:
   - **IN** records the IN jack, ±5 V full scale;
   - **KIT** resamples the kit's own output;
   - **IN + KIT** records their sum.
3. Patch a clock into **CLOCK** (Gorgon's CLOCK is one pulse per row, four per
   beat, the default) and press **ARM**, or trigger the ARM input. The take
   starts on the next clock pulse and lasts LENGTH beats of pulses. Without
   CLOCK it starts at once and lasts LENGTH beats at **TEMPO**.
4. **REC** is high while the take runs, and **DONE** pulses (1 ms) when it ends.
   The slot then plays the take on its next hit.

**GAIN** scales what is recorded, 0–200%.

## Timing

- A take starts on the sample of the clock pulse that follows ARM, and covers
  every sample up to, but not including, the pulse that ends it: LENGTH × PPQN
  pulses later. A length shorter than one pulse (1/4 beat at 1 or 2 pulses per
  beat) takes one pulse.
- Without CLOCK, a take of *b* beats is `round(b × 60 / TEMPO × sample rate)`
  samples, from the sample ARM arrives.
- Capture and the kit pass each sample through Rack's expander messages, so the
  kit writes the take one sample after Capture decides. The flag and the audio
  travel together, so the take holds exactly the audio from its start sample on.
- ARM while a take is running does nothing; a take is never restarted part-way.
- Clock pulses per beat (1, 2, 4, 8, 12 or 24) are in the context menu and saved
  with the patch.

## Where takes go

The kit has one ES5505 sample memory, about 21.8 s at 48 kHz. Takes are written
straight into its free memory at Rack's sample rate, so nothing is resampled,
copied or allocated on the audio thread. A take's rate becomes its slot's pitch
offset, as for a loaded WAV, so it plays back at its recorded pitch at TUNE 0.

- **Free memory** is every part of memory that is not a slot's current take, not
  a replaced take still sounding, and not being saved. A take is written into the
  longest such stretch. The display's ROOM shows how long a take can be, or, while
  one is recording, how much more it can hold.
- **When a take ends** it becomes its slot's take at once. A hit on that slot that
  started earlier keeps playing the replaced take to its end, or until the next
  hit fades it out (OTIS's 1.5 ms declick). Only then is the replaced take's
  memory free again. Every other slot is untouched throughout.
- **No room:** a take that cannot start (no free stretch of two frames) is not
  made; the display says **NO ROOM** and the slot keeps its take.
- **A take that outgrows its stretch** is cut at the end of it and still lands;
  the display marks it **CUT**.
- **Moving Capture away** mid-take ends the take there.
- **Compact the bank** (Otis Kit's menu) packs every slot, takes included, back
  into one run of memory, which joins the free stretches. It stops sounding hits
  and drops a take in progress, as loading a WAV does.

## Saving

The kit saves its takes as ordinary slots: 16-bit PCM at their rate, named
"Capture take *n*". Saving never stops the audio thread. It copies the takes out
of memory while the kit keeps running, after marking them so that no new take
is recorded over them; if a take finishes while it copies, it starts again. A
take still recording when the patch is saved is not in the save: the slot keeps
its previous take. Capture saves only its pulses-per-beat setting.

## Controls and ports

| Item | Behavior |
|---|---|
| SLOT | Slot 1–8 the next take goes into. |
| LENGTH | 1/4, 1/2, 1, 2, 3, 4 (default), 6, 8, 12, 16 or 32 beats. |
| SOURCE | IN, KIT or IN + KIT. |
| GAIN | 0–200% of the recorded signal. |
| TEMPO | 40–240 BPM; used only without CLOCK. |
| ARM | Button and trigger input: start the next take. The light shows it is waiting for a pulse. |
| IN | Audio, ±5 V full scale. |
| CLOCK | Pulses (rising at 2 V, falling at 0.1 V). |
| REC | 10 V while a take records; the red light shows it too. |
| DONE | 1 ms trigger when a take ends. |

## Validation

- `tests/kit_capture_test.cpp` (and its ASan/UBSan and TSan builds): the
  free-memory allocator against a brute-force map over random takes, sounding
  takes, pins and a take in progress; takes written in order and made current at
  the host rate; replaced takes kept while sounding; short, truncated and
  no-room takes; in 300,000 random steps of hits, takes and save pins, no write
  into a current, sounding or pinned take; and a saving thread copying takes
  while another records them, every copy one whole take, race-free under TSan.
- `tests/capture_module_test.cpp` (and its ASan/UBSan build), with Rack's
  expander wiring and message swaps emulated:
  - no kit;
  - a clock-quantized one-beat take starting on the pulse after ARM and ending
    on the fourth pulse after that, its content equal to the input sample for
    sample, in either processing order;
  - while it lands, a hit on the same slot that started earlier and a hit on
    another slot both play on bit-identically to a kit without Capture;
  - the next hit plays the take exactly as a kit loaded with it;
  - save and restore of the take;
  - resampling the kit at half gain;
  - no room, a cut take, a save during a take, TEMPO length, Capture moved away
    mid-take, and compaction keeping every take.
- `tests/plugin_test.cpp`: the compiled plugin has OTIS Sampler, Otis Kit and
  Otis Capture.
- OTIS Sampler's legacy trace is unchanged.

Physical audio interfaces and live performance are outside these tests.
