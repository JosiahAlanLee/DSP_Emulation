# CZ-101 owner capture set

These are **test inputs, not hardware recordings**. No trim procedure or
calibration has been performed by this project. The analog model and several
firmware curves remain provisional until a measured comparison exists.

`cz101-capture-fixtures.zip` contains 98 original single-tone SysEx files,
matching standard MIDI note clips, and `manifest.tsv`. The unpacked manifest is
also committed beside it. `tools/render.cpp` authors these settings; none are
factory tones. `tools/check_capture.py` rebuilds the archive in a temporary
directory, checks every byte and MIDI note duration, and validates complete
note-off/end-of-track events. The archive has fixed timestamps.

To regenerate intentionally:

```sh
make -C vcv/czish/tests render
python3 vcv/czish/tools/check_capture.py --write
```

## Capture

1. Record the untouched setup first if its present grit is worth comparing.
   Any physical adjustment is the owner's separate decision, using the correct
   service procedure for that unit. Do not infer a trim method from this code.
2. Unzip the fixtures. Load one `.syx` through a CZ-compatible librarian. These
   are recorded tone-dump files; the librarian handles the keyboard's handshake.
3. Put the CZ in poly mode and transfer each tone before playing its `.mid`.
   The MIDI clips contain notes and tempo, not tone uploads. The mode/line
   selection lives in the tone. Most notes are held for 3–8 seconds; slow
   envelope rate ladders can be 30 or 120 seconds. Review the manifest first.
4. Record line out into the Digi 003 line input at 24-bit and the highest
   practical sample rate. Keep interface gain, output gain, cabling and sample
   rate fixed across a comparison. Leave headroom and avoid automatic gain,
   normalization, noise reduction, effects or lossy encoding.
5. Name the WAV after its fixture. Record the interface model, input type,
   recording rate, gain settings, keyboard output setting and trim status in a
   separate session note. A physical clock/loopback reference helps separate
   MIDI and interface latency from envelope timing.

| Prefix | Test |
|---|---|
| a | Silence/noise floor |
| b | Cosine at four DCA levels |
| c | Eight 10 ms tone bursts, separated by 500 ms |
| d | 1, 2, 4 and 8-note chords; summed/per-slot compander hypotheses |
| e | Full-DCW saw at five octaves |
| f | DCO/DCW/DCA rate and level ladders, ten points each |
| g | Vibrato rate and depth ladders |
| h | DCW/DCA key follow at 0, 5, 9 and three pitches |
| i | Ring pair and each partner alone |

Rate/level references describe software/Casio panel units, not elapsed
milliseconds. Slow settings may need longer recordings if a full settled tail
is wanted. The 10 ms bursts test compander memory; their hardware envelope
attacks must also be measured, not assumed instantaneous.

## Software reference

The same single-tone file can be rendered without Rack:

```sh
vcv/czish/tests/render unpacked/b-cosine-level-99.syx software.wav 60 4
vcv/czish/tests/render unpacked/b-cosine-level-99.syx native.wav 60 4 native
```

The ordinary output is 48 kHz, CLEAN reconstruction, ideal instantaneous CZ
model and 8× audition gain. The `native` output is the 40 kHz **unconverted chip
sum**, with gain 1. Both are mono 16-bit PCM; the gate is high for 65% of the
requested duration, then releases. For the burst/poly MIDI sequences, render
notes from the corresponding clip or use the Rack patch rather than treating a
single-note reference as the same sequence.

Fit and report the converter law, sum/slot behavior, output filter, envelope
maps, vibrato timing and key follow separately. A low numerical residual after
fitting is not independent validation; reserve some pitches, levels and
polyphony combinations as hold-out recordings. Do not ship ROM images or
manufacturer tone banks with the results.
