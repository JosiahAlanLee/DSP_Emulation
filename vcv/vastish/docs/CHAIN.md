# Vastish chain mode

Contract for VastishLab 2.8, linked from the [manual](MANUAL.md#chain-mode).
A Vastish placed directly to the right of another can continue its voices: each
voice takes the same-numbered voice of the Vastish on its left, after that
voice's three function blocks and before its amp, in place of its own
oscillator, and plays it on the left one's notes and gates. Three in a row is
the K2600's triple modular processing role: one sound through nine DSP blocks.
This is an original implementation; the K2600 is the role reference only.

## Turning it on

- Context menu **Chain from the Vastish on the left**, saved in the patch as
  `"chainFromLeft": true`. It is off by default and off after Rack's
  Initialize, and a patch without it (2.7 and earlier) opens with it off, so
  Vastishes already side by side in a patch don't start chaining.
- It takes effect only with a VastishLab Vastish directly to the left. With
  nothing there, or another module, this Vastish plays as usual, and the menu
  says so ("place this Vastish directly right of another").
- The Vastish on the left needs no setting: every Vastish sends its voices to a
  Vastish on its right, whether or not that one chains. Its own L/R and other
  outputs are unchanged.

## What a chained voice does

Every sample, the left Vastish sends, for each of its eight voices:

- **The signal**: the voice's tap, `r[6]·a + r[7]·b + r[8]·c` of its algorithm,
  which is the voice before AMP, PAN and the envelopes. It is also what BUS OUT
  sends as its DSP source.
- **The note, velocity and gate** in force on that voice, and whether the voice
  runs without a gate (an AUDIO voice with GATE unpatched, or a chained one that
  is itself gate-free).
- **Its voice count**, which becomes the chained Vastish's.

The chained Vastish uses them as follows:

- The signal replaces the oscillator, whatever the PITCH wave: it runs through
  this Vastish's own algorithm, F1–F3, AMP and PAN. It is not clipped to ±1, as
  AUDIO IN is, because a chain's internal level can exceed full scale; a safety
  limit of ±16 only catches runaway values, and a non-finite value reads 0.
- Notes, velocities and gates replace this Vastish's PITCH, GATE and VEL
  inputs, which are ignored while chained. Its own envelopes, LFOs and FUNs run
  on those notes, and its AMP envelope shapes the result, as the default asks:
  each Vastish in the chain applies its own amp. Velocity is latched on each
  gate rise, as for any note.
- A voice that is gate-free on the left is gate-free here too: it runs at unity
  amp without a gate, so an AUDIO-IN Vastish can feed a chain continuously.
- MOD, AUX 1–4 and BUS A/B are still this Vastish's own.
- The PITCH wave's oscillator, the SAMPLE wave and AUDIO IN are unused while
  chained; key tracking uses the chained note.

## Timing

The data travels through Rack's expander messages: the right Vastish owns two
buffers, the left one writes one each sample and Rack swaps them at the end of
the engine frame. So each hop is exactly one sample late, whatever order Rack
processes the modules in, and a chain of three is two samples behind its first
module. That is the same one-sample cost as a cable.

## Saving and limits

Only the on/off setting is saved. The chained Vastish has no memory of the left
one; moving it away ends the chain at once, and putting it back resumes it.
Turning chain mode on or off hands each voice from one set of gates to the
other; a voice whose gate is high on both sides continues without retriggering.
Panic silences the chained voices, but gates still high on the left sound again
on the next sample, since they come from the chain.

Stereo is not part of the chain: a left voice's SAMPLE right channel (`blockR`)
is not sent, so a stereo sample continues as its left channel.

## Validation

- `tests/engine_test.cpp`: the chain input replaces every wave at unity through
  NONE blocks when gate-free; without a gate it is silent and the amp envelope
  shapes it with one; non-finite input reads 0 and runaway input is limited.
- `tests/adapter_test.cpp`, with Rack's expander wiring and end-of-frame message
  swap emulated and the modules processed in a random order every frame: three
  Vastishes in a row, the first on AUDIO IN gate-free, give exactly the first
  one's input one and two samples later through their DC blockers; chained
  notes, velocities and gates follow a three-voice keyboard while the chained
  module's own GATE is ignored; chain off, no module on the left, or a different
  module there all play as unchained; the setting round-trips through the patch
  and is off when absent.
- `tests/plugin_test.cpp`: Rack's Initialize turns it off.
- The legacy trace of an unchained Vastish is unchanged.

Physical audio interfaces and live performance are outside these tests.
