# Vastish on the byte bus

Contract **v0.2**, VastishLab 2.5. The [shared protocol](../../docs/BYTE_BUS_SPEC.md)
defines bit order, levels, thresholds, conversions and cable timing. This
contract defines the original extensions; existing IDs and default audio remain stable.

## Ports, parameters and sources

Append BUS A/B inputs 8/9 and BUS OUT output 11. Parameters 80/81 are BUS OUT
MODE/SOURCE; 82–85 are FUN1–4 BIT flags. There are 86 parameters, 10 inputs
and 12 outputs. Sources 18–22 are BUS A, BUS B, LANE A, LANE B and ONE.
BUS is wave 6; SAMPLE retains wave 5. All old IDs remain unchanged.

Each bus has its own Reader and optional ninth-channel clock. BUS A/B sources
are `(word-128)/128` in every voice. LANE A/B is bit v as 0 or 1 in voice v.
Unconnected buses read zero as sources, lanes and BUS wave. Connected all-low
means word 00, hence bipolar -1. ONE is always 1 without a gate. For connected
A, `(BUS A + ONE)/2` gives n/256 phase. AUX retains its original CV scaling and
mono broadcast; it does not become a clocked bus reader.

## FUN composition

Equations retain their numeric results by default. A saved context-menu BIT
flag per FUN replaces that FUN's result with 1 if finite and positive, else 0,
before downstream FUNs read it. Thus BIT(A+B) is a composable OR without value
2 propagating through the expression. A*B/MIN is AND; ABS(A-B) is XOR;
BIT(A-B) is A AND NOT B; ONE-B is NOT. Cyclic dependency edges still read zero.
FUN jacks remain result times 10 V. The LANES encoder uses the same positive test.

## Control and DSP execution

While either bus is connected, or BUS OUT is LANES, all eight voices evaluate
gate changes, envelopes, LFOs, sources and FUNs every sample. Modulation jacks
then emit eight channels. Audio polyphony remains controlled by PITCH/GATE and
release tails. A sleeping voice runs controls without its oscillator/filters.

DSP is the chain before AMP, pan, MASTER and DC blocking. WORD/DSP uses voice
1's left chain. LANES/DSP explicitly runs all eight chains even without gates,
so those taps cannot become stale. L/R still require an amplitude envelope.
Active-voice audio is identical with additional sleeping-voice controls enabled
or disabled. The processing path performs no allocation.

## BUS wave

BUS uses BUS A's held bipolar word in place of the oscillator. PITCH rotates
it using `wrap[-1,1)(x + 2*(control-60)/12)`, with the full finite control
value, not a clamped oscillator frequency. ADJUST 60 / KEY 0 is identity;
66 rotates half a cycle and 72 a full cycle. Disconnected A supplies zero
independent of rotation. One volt through KEY 1 is one complete turn.

The DSP chain advances at the engine rate between upstream clock edges.
NONE, SHAPER, DIST, GAIN and CRUSH provide memoryless transforms; filters,
comb, decimation and oscillator blocks provide stateful transforms. The
pre-amp tap continues without gates. This is Rack-rate, potentially aliased
processing, not MHz hardware timing.

## BUS OUT

Saved context-menu MODE selects Neutral, WORD or LANES, default WORD. SOURCE
selects the 23 sources, DSP (23) or MIX (24), default DSP.

- WORD encodes the selected voice-1 value with fromBipolar. DSP uses the left
  chain; MIX uses `(L+R)/2` after MASTER/DC blocking. A source's +/-1 is full
  scale even though the corresponding ordinary FUN jack uses +/-10 V.
- LANES sets bit v if the selected value in voice v is finite and positive.
  MIX supplies the same value to every bit.
- Neutral immediately emits 80 on an eight-channel cable. Rack cannot make
  a connected output disappear using setChannels(0). Neutral is zero bipolar
  signal but lane 7 is high; physically disconnect to remove lane activity.

The primary clock is connected BUS A, otherwise BUS B. Unclocked A takes
precedence over clocked B. With a clocked primary, encoding (including control
changes) updates only on its rising edge and forwards its clock on channel 9.
Otherwise it updates every sample on eight channels. Both input readers retain
their own independent latches.

The output latch starts at 80; Neutral resets it. Changing source/mode does not
invent an acquisition edge. Input decoding precedes control/DSP evaluation
and encoding in the same process call. Each cable hop adds the normal Rack
one-sample delay; no additional output register stage is added.

## Programs, compatibility and compiler

Loading a starting program restores original sound parameters 0–79 and resets
BIT flags to off; it preserves BUS OUT mode/source as patch routing. Old
patches get the new defaults. Rebuild Programs.hpp through import_programs.py.

The offline compiler accepts legacy 80-parameter/18-source snapshots, padding
appended entries from current defaults. ONE is forced to 1. Newly compiled
tables record complete current snapshots. Native BUS fields in a frozen recipe
are explicit frozen values; they do not acquire a live cable during compilation.

## Acceptance

All 256 BUS/NONE-chain words and half/full turns round-trip exactly. Tests cover
FUN truth tables and normalized chains, eight control lanes without gates,
stateful gate-free DSP, clock holds and dual-clock priority, short cables,
Neutral mode, persistence, program-load routing and zero steady allocations.
Original mono/stereo audio traces and Pd/sample-chain comparisons must pass.
Native delayed converter/Register/Vastish/Lookup/OTIS patches verify timing
and frame selection at 44.1/48/96 kHz.
