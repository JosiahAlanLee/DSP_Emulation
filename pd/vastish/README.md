# vastish

An 8-voice, VAST-flavoured synth in vanilla Pure Data (0.52 or later; developed on 0.55). The pitch block feeds three DSP function slots and an amp stage. An algorithm decides how the slots are wired. Every modulatable parameter has a K2000-style control path:

    value = Adjust + KeyTrack*(key-60) + VelTrack*vel + Src1*Depth1 + Src2*Depth2

Frequency-type parameters are note numbers (60 = middle C), so `adj 60, kt 1` tracks the keyboard.

## Use
Open `vastish.pd` and turn DSP on. Click a program (0–5), then play MIDI on any channel; CC1 is the mod wheel. Open `[pd edit]` to change parameters. The panel follows program changes.
To record, click the record message, then the stop message. Takes are written next to the patch.

To edit a program permanently, edit the matching message in `[pd programs]`, or regenerate everything from `gen_vastish.py`, which is the real source.

## Blocks
- **Pitch**: wave 0 saw / 1 pulse (pw) / 2 tri / 3 sine / 4 noise. The waves are naive and alias.
- **F1–F3** (`type`):
  - 0 NONE
  - 1 LOPASS (1-pole)
  - 2 HIPASS (1-pole)
  - 3 LP2RES (b = Q)
  - 4 BANDPASS (b = Q)
  - 5 NOTCH (b = Q)
  - 6 LP4 (bob~ ladder, b = res 0–1)
  - 7 SHAPER (sine fold, adj ≈ 0.05–16)
  - 8 DIST (adj = drive dB)
  - 9 SINE (osc, ignores input)
  - 10 RINGMOD
  - 11 GAIN (dB)
  - 12 CRUSH (bits)
  - 13 DECIMATE (rate as note#)
  - 14 COMB (tuning as note#, b = feedback)
  
  Unused functions are switched off with `switch~`, so they cost no CPU.
- **Amp**: `amp-*` control path in dB, multiplied by AMPENV². **Pan** has its own control path (-1..1).

## Algorithms
1. P→F1→F2→F3
2. (P→F1 + P→F2)→F3
3. P→F1→(F2 + F3)
4. (P→F1 × P→F2)→F3 (ring)
5. P→F1 + P→F2 + P→F3
6. P→F1→F2 + P→F3

## Mod sources
0 OFF, 1 ENV2, 2 ENV3, 3 LFO1, 4 LFO2, 5 FUN1, 6 FUN2, 7 MWHEEL, 8 VEL, 9 KEYNUM, 10 RAND (per note), 11 AMPENV.

LFOs are per voice and reset on note-on (shape 0 sin, 1 tri, 2 saw, 3 square, 4 s&h).

FUN equations take sources a and b: 0 a+b, 1 a-b, 2 a×b, 3 avg, 4 max, 5 min, 6 |a-b|, 7 a×(b+1)/2.

## Programs
0 INIT, 1 Ladder Bass, 2 Ring Bell, 3 Fold Pad (mod wheel folds harder), 4 Crush Lead, 5 Comb Pluck.

## Files
- `vvoice` is one voice.
- `vblock` is one function slot.
- `vctl` is the control path.
- `vadsr`, `vlfo` and `vfun` are the modulation sources.

Parameters are global `vast-*` sends, so anything that can send a message can drive them.

## Known limits
- No per-layer stacking.
- No ASRs.
- No per-program FUN feedback.
- COMB at high feedback on a raw saw can hit full scale.

## Regenerating / testing
`tools/gen_vastish.py` is the source of truth; running it rewrites all `.pd` files in this folder (hand edits to the patches will be lost). `tools/render.sh` renders programs 1–5 headless to `renders/` and prints level/centroid stats. `demo/vastish-demo.mp3` is that render, programs 1–5 in order.
