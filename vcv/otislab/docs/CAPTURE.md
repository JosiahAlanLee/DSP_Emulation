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
   starts on the first clock pulse at or after ARM and lasts LENGTH beats of
   pulses. Without CLOCK it starts at once and lasts LENGTH beats at **TEMPO**.
4. **REC** is high while the take runs, and **DONE** pulses (1 ms) when it ends.
   The slot plays the take from its next hit.

**GAIN** scales what is recorded, 0–200%.

## Timing

- A take starts on the sample of the first clock pulse at or after ARM, and
  covers every sample up to, but not including, the pulse that ends it:
  `max(1, round(LENGTH × PPQN))` pulses later. A length shorter than one pulse (1/4 beat at 1 or 2 pulses per
  beat) takes one pulse.
- Without CLOCK, a take of *b* beats is `round(b × 60 / TEMPO × sample rate)`
  samples, from the sample ARM arrives.
- Capture and the kit pass each sample through Rack's expander messages, so the
  kit writes the take one sample after Capture decides. The flag and the audio
  travel together for IN, so it holds exactly the external audio from its start
  sample on. KIT uses the kit's output on the receiving sample, one sample later;
  IN + KIT sums those two signals. SLOT is read when the kit receives the start;
  SOURCE and GAIN are read throughout the take. LENGTH, PPQN and TEMPO set the
  duration when Capture starts. A take started with CLOCK ends on pulses while
  CLOCK remains patched. If CLOCK is removed, it ends at its TEMPO duration
  measured from the start (at once if that duration has already elapsed). A
  take started without CLOCK ignores a clock patched part-way.
- ARM while a take is running does nothing; it cannot restart a take part-way.
- Clock pulses per beat (1, 2, 4, 8, 12 or 24) are in the context menu and saved
  with the patch.

## Where takes go

The kit has one ES5505 sample memory, 1,048,320 frames, about 21.8 s at 48 kHz.
Takes are written straight into its free memory at Rack's sample rate, so nothing is resampled,
copied or allocated on the audio thread. A take's rate becomes its slot's pitch
offset, as for a loaded WAV, so it plays back at its recorded pitch at TUNE 0.

- **Free memory** is every part of memory that is not a slot's current take, not
  a replaced take still sounding, and not being saved. A take is written into the
  longest such stretch. The display's ROOM shows how long a take can be, or, while
  one is recording, how much more it can hold.
- **When a take ends** the kit makes it its slot's take on receiving the end,
  after that sample's hits. Takes shorter than two frames are discarded. A hit
  on that slot that started earlier keeps playing the replaced take to its end, or until the next
  hit. Known limit: during the 1.5 ms fade before a retrigger, the voice already
  reads the new take. Every other slot is untouched throughout.
- **No room:** a take that cannot start (no free stretch of two frames) is not
  made; the slot keeps its take. REC still stays high for the requested duration
  and DONE pulses at its end; **NO ROOM** appears once REC and ARMED are off.
- **A take that outgrows its stretch** is cut at the end of it and still lands;
  the display marks it **CUT** after the requested duration ends. REC and DONE
  keep their scheduled timing even after memory fills.
- **Moving Capture away** mid-take ends the take there.
- **Compact the bank** (Otis Kit's menu) packs every slot, takes included, back
  into one run of memory, which joins the free stretches. It stops sounding hits
  and drops a take in progress, as loading a WAV does; a take that finishes
  while it packs is kept. Changing Rack's sample rate also drops a take in
  progress. Nothing more is recorded
  until Capture ends that request and starts a new take; REC and DONE keep
  their timing.

## Saving

The kit saves its takes as ordinary slots: 16-bit PCM at their rate, named
"Capture take *n*". Saving never stops the audio thread. It copies the takes out
of memory while the kit keeps running, after marking them so that no new take
is recorded over them; if a take finishes while it copies, it starts again. A
take still recording when the patch is saved is not in the save: the slot keeps
its previous take. Rack saves Capture's controls too; its additional patch data
is only the pulses-per-beat setting. Arming and recording state are not saved.

## Controls and ports

| Item | Behavior |
|---|---|
| SLOT | Slot 1–8 the next take goes into; default 1. |
| LENGTH | 1/4, 1/2, 1, 2, 3, 4 (default), 6, 8, 12, 16 or 32 beats. |
| SOURCE | IN (default), KIT or IN + KIT. |
| GAIN | 0–200% of the recorded signal, default 100%; clipped to 16-bit PCM full scale. |
| TEMPO | 40–240 BPM in whole BPM, default 120; used only without CLOCK. |
| ARM | Button and trigger input (rising at 2 V, falling at 0.1 V): arm the next take. The light shows it is waiting for a pulse. |
| IN | Audio, ±5 V full scale. |
| CLOCK | Pulses (rising at 2 V, falling at 0.1 V). |
| REC | 10 V while a take records; the red light shows it too. |
| DONE | 10 V, 1 ms trigger when Capture's recording interval ends, even if no take fits. |

## Validation

- `tests/kit_capture_test.cpp` (and its ASan/UBSan and TSan builds): the
  free-memory allocator against a brute-force map over random takes, sounding
  takes, pins and a take in progress; takes written in order and made current at
  the host rate; replaced takes kept while sounding; short, truncated and
  no-room takes; in 300,000 random steps of hits, takes and save pins, no write
  into a current, sounding or pinned take; and a saving thread copying takes
  while another records them, every copy one whole take, race-free under TSan;
  a new generation for every finished take, even one in an older take's region.
- `tests/capture_module_test.cpp` (and its ASan/UBSan build), with Rack's
  expander wiring and message swaps emulated:
  - no kit;
  - a clock-quantized one-beat take starting on a pulse after ARM and ending
    on the fourth pulse after that, its content equal to the input sample for
    sample, in either processing order;
  - while it lands, a hit on the same slot that started earlier and a hit on
    another slot both play on bit-identically to a kit without Capture;
  - the next hit plays the take exactly as a kit loaded with it;
  - save and restore of the take's audio, name and rate;
  - resampling the kit at half gain;
  - no room, a cut take, a save during a take, TEMPO length, Capture moved away
    mid-take, and compaction keeping every completed take;
  - loading or compacting during a take discards it without recording its tail,
    with REC and DONE unchanged and the next take recording normally;
  - CLOCK removed before or after the TEMPO deadline, CLOCK patched during an
    unclocked take, and a slow clock kept patched past the TEMPO deadline.
  - three equal-length takes into one slot of a nearly full bank, the third in
    the first's region: save and Compact keep the third;
  - a take finishing while Compact or a WAV load waits for the engine lock is
    kept;
  - a take recorded at 768 kHz saved and reloaded at its rate;
  - a sample-rate change mid-take drops it, and the next take records at the
    new rate.
- `tests/plugin_test.cpp`: the compiled plugin has OTIS Sampler, Otis Kit and
  Otis Capture.
- OTIS Sampler's legacy trace is unchanged.

Physical audio interfaces and live performance are outside these tests.
