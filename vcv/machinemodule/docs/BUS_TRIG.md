# Machinemodule: bus TRIG mode

Contract for MachinemoduleRack 2.1, linked from the adapter README's "Play
it" section. A patch-saved option on the existing **TRIG** input; no new jack,
no change to the [byte bus](../../../docs/BYTE_BUS_SPEC.md) or to the Gorgon
sound link.

## Why

A byte bus is a poly cable of 0/10 V lines, so it already plugs into TRIG. In
the per-track mode that works only by accident: bit *k* rising fires track
*k*+1, channel 9 (CLK) fires track 9 on every publication, and a bit held high
across publications never retriggers, which is what Lookup's Rhythm family and
a Counter's high bits do. A nine-channel cable is ambiguous between "nine
tracks" and "eight bits and a clock", so the module cannot guess.

## Modes

Context menu **TRIG reads**: **Tracks** (the 2.0 behaviour, default) or
**Bus**. Saved in the patch as `"trigMode": "tracks"` or `"bus"` alongside the
firmware path; a patch without it, such as one saved by 2.0, reads Tracks.
Switching modes seeds the detectors as a cable hot-plug does, so no hit
replays.

**Tracks** is unchanged: up to sixteen channels, channel = track, a hit on each
channel's rising edge through the existing per-channel detector.

**Bus** decodes TRIG with the shared `dspemu::bus::Reader`:

- **Clocked (nine or more channels):** on an engine sample where the Reader
  reports a rising CLK edge, track *k*+1 (k = 0…7) hits iff bit *k* of
  `rawWord` is high on that same sample. Nothing happens between edges. A bit
  that stays high across several publications hits on each one. Channels above
  CLK are ignored; there are no tracks 9–16 in this mode.
- **Unclocked (eight or fewer channels):** no CLK, so bus mode falls back to
  per-bit rising edges on bits 0–7, identical to Tracks mode restricted to
  eight channels. Bus mode therefore only differs from Tracks when a clock
  channel is present.

Velocity, MACHINE CV, P1–P8 CV, RESET and the queue, generation and block
rules are unchanged: a bus hit is `appendHit(k, velocity, {}, offset)` with the
same velocity source as an external trigger (VELOCITY channel *k*, mono
broadcast, 127 when unpatched).

## Edge semantics

These are the bus spec's §5 and nothing else. The Reader decodes data and CLK
through the same Schmitt thresholds (1.0 V / 0.1 V), on the same cable, in the
same engine sample, so there is no skew between a bit and its clock and module
processing order cannot split them. The one-sample cable hop is the only
latency this adds, and it is already documented for every bus hop.

One deliberate deviation from a Reader used for words: the bus spec counts an
initially high CLK as a first rising edge, which is right for latching a word
and wrong for a trigger consumer, where it replays whatever was high. In bus
mode the first CLK edge after cable connection, a mode switch or an engine
restart (OS load, sample-rate change) **arms** the detector and does not hit;
hits begin at the second edge. A bus that gains or loses its CLK channel starts
over the same way: a newly clocked bus arms again, and a bus that becomes
unclocked seeds its bit detectors. This mirrors the existing "seed edges on a
source switch" rule for Tracks mode. A Register or Gorgon seed strobe arriving
later is an ordinary edge and hits.

### Starting a chain

When a cable is connected, Rack gives its output one channel at 0 V until the
sending module first writes it. A transparent module between the source and
Machinemodule, such as Lookup, passes that one sample on as an unclocked word,
its output for address 00, one sample before the clocked bus reaches it. Bus
mode's unclocked path plays that word once. This happens when a cable into such
a chain is connected while Machinemodule is running. At patch load the OS is
still loading, so nothing plays. A Counter or Register patched directly into
TRIG publishes a clocked bus from its first sample and does not do this.

## Timing

A bus hit enters the renderer through the same path as an external trigger and
is subject to the same forward rounding to the native 32-sample block boundary
and the same per-track once-per-block coalescing. Hits for several tracks on
one CLK edge are simultaneous in the event queue, as a Gorgon packet's are.
The documented queue latency is unchanged.

## What this is not

- Not a byte-as-parameter path. Converter DAC → P*n* CV is that, today.
- Not a sixteen-track bus. A second TRIG for tracks 9–16 is a later decision,
  not assumed here.
- Not a change to the sound link. The link is used only when TRIG is
  unpatched; a patched bus overrides it exactly as a patched trigger cable does.

## Validation

Module tests (`tests/module_test.cpp`, Rack API, firmware-free fake engine):
a nine-channel bus with bits 0, 2, 5, 7 high and a CLK pulse every 64 samples
hits tracks 1, 3, 6, 8 on each pulse after the arming one and no others,
although the bits never change; the same cable in Tracks mode hits track 9 on
every pulse and never retriggers the held bits; an eight-channel bus hits on
per-bit edges only; a hot-plug and a mode switch, each with CLK high and with
CLK low, produce no hit until the second edge; VELOCITY per channel, mono
broadcast and unpatched; JSON save/restore of the mode, and a patch without
it. When ByteBusLab's sources sit beside this directory, as in the repository,
a Counter → Lookup (Rhythm) → Machinemodule chain through one-sample cables in
random process order plays the hit list the chain gives evaluated directly,
including the startup word above, for three orderings.

Live, 2026-10-26: VCV Rack Free 2.6.6, headless, on a private JACK server at
48 kHz, with OS 1.63 and machine 16 on every track. Counter (internal 8 Hz) →
Lookup Rhythm → Machinemodule in Bus mode, recording tracks 1–9 for 16 s. All
41 expected hits sounded on their tracks, 30 of them bits held high from the
previous step, with none on tracks whose bit was low; track 9 stayed silent.
Hits starting from silence sounded 1126 samples (23.5 ms) after the bus edge,
within −4 to +5 samples. The firmware is not a CI fixture, so this was run on
the workstation only.
