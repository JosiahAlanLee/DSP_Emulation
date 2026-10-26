# Vastish AUDIO IN

Contract for VastishLab 2.6, linked from the [manual](MANUAL.md#ports-and-polyphony)'s
inputs table. External audio replaces the oscillator block of a voice and runs
through its DSP algorithm, envelopes, FUNs and pan: the K2500's Live Mode role,
as an original implementation. Two steps: **mono** first (2.6.0), then
**polyphonic**. The Pd Vastish is unchanged; this is native-only, like the
SAMPLE wave.

## Ports and parameters

- New input **AUDIO IN**, input ID 10, appended after BUS B (ID 9). Existing
  input, output and parameter IDs are unchanged.
- New PITCH wave choice **AUDIO**, value 7, appended after BUS (6).
  `constexpr int AUDIO_WAVE=7;` and every `choice(p[PITCH_WAVE],BUS_WAVE)`
  becomes `choice(p[PITCH_WAVE],AUDIO_WAVE)`.
- Context menu **AUDIO IN range**: ±5 V (default, matching L/R's `y*5`
  output scaling, so unity through NONE blocks is unity) or ±10 V. Saved in
  the patch as `"audioInRange": 5` or `10`. A patch without it, such as one
  saved by 2.5, opens at ±5 V; Rack's Initialize restores ±5 V.

## Mono step

With wave AUDIO, voice 1's oscillator sample `x` is AUDIO IN channel 1 scaled
by the range: `x = clamp(v / range, −1, 1)`; non-finite reads 0. Everything
after `x` is today's code: the three function blocks and routing algorithm,
`tap`, amp, pan, the L/R outputs, BUS OUT's DSP source. PITCH still supplies
`in.note` to `control(F*_ADJ, …)`, `AMP_ADJ` and `PAN_ADJ`, so V/OCT key-tracks
the filters and shapers as it does for an oscillator; PITCH_PW and the BUS
rotation are unused.

**Gate-free processing.** When wave is AUDIO, AUDIO IN is patched and GATE is
unpatched, the voice runs its audio every sample and its amp stage is unity:
the `sources[11]²` factor (amp envelope) is replaced by 1. Envelopes do not
run without a gate; LFOs and FUNs run as they do today for a bus-driven voice.
With no gate to hold it, the voice's note follows PITCH continuously, so
key tracking works without notes. Velocity is only latched by a gate, so VEL
tracking and the VEL source keep the last latched value, 0 if the voice has
never been gated. When GATE is patched, behaviour is unchanged — the amp
envelope gates the processed audio, which makes Vastish a VCA with a VAST
chain in front of it, and pitch holds between notes as for every wave.

With AUDIO IN unpatched, wave AUDIO is a silent input under the usual amp
envelope, so blocks that self-oscillate stay silent without a gate.

The decision is per voice: `VoiceInput` gains `audio`, `audioConnected` and
`gateConnected`, and `tickAudio` replaces the envelope factor when
`audioConnected && !gateConnected`. The engine runs voice 1 every sample
whatever its gate, since the voice count is at least one, so the mono step
does not change which voices run.

In the mono step the voice count is still `max(PITCH, GATE)` channels, floor 1;
AUDIO IN channel 1 feeds voice 1 and the other voices read 0.

## Polyphonic step

Voice count becomes `max(PITCH, GATE, AUDIO IN)` channels, up to eight, the
way PITCH and GATE establish voices now. Voice *v* reads AUDIO IN channel *v*;
a mono AUDIO IN cable broadcasts to every voice. So Machinemodule's sixteen
TRACKS (first eight channels) or an ES-6's returns run through eight VAST
chains with one cable, each chain key-tracked by its own PITCH channel.
Voices that AUDIO IN establishes are inside the voice count, so they already
run every sample; the engine's `audioAll` path, which runs all eight, is not
needed.

Stereo is not a feature of this contract: the SAMPLE wave's right-channel
path (`blockR`) is not used. A stereo pair is two voices, panned by PAN or by
FUN. Running AUDIO IN through `blockR` as a true stereo voice is a later
change.

## Feedback

A patch that returns Vastish's output to AUDIO IN is an ordinary Rack
one-sample feedback loop. The output DC blocker and the non-finite panic in
`Engine::tick` apply; nothing else is added. A loop with net gain above unity
will limit at the chain's own nonlinearities or at the ±1 clamp on input;
that is the patch's business.

## Programs and compatibility

- A program saved with wave AUDIO reloads as AUDIO. Opened in Vastish 2.5,
  the value 7 clamps to BUS (6) and the voice is silent without a bus. The
  manual's saving notes say so; the value is not remapped.
- The Vastish table compiler (ByteBusLab's `CompileVastish.hpp`) compiles
  memoryless patches from a constant input; an AUDIO-wave program is not a
  table, and the compiler rejects it: "PITCH wave AUDIO processes live input
  and cannot be compiled to a table; choose another wave".
- The pre-bus trace comparison and the Pd reference comparison are unaffected:
  neither uses the new wave.

## Validation

Portable engine tests (`tests/engine_test.cpp`): with wave AUDIO, NONE blocks
and algorithm 1, an impulse and a 100 Hz sine pass to the pre-pan output
exactly, and through the engine they match the DC blocker's response; each
of the fourteen block types and NONE processes AUDIO in all six algorithms
without faults at 44.1/48/96 kHz; gate-free unity vs gated envelope; an
unpatched AUDIO IN is silent; key-tracked F-block ADJ follows `in.note`;
non-finite input reads 0 and input beyond ±1 clamps.

Rack API tests (`tests/adapter_test.cpp`, `tests/plugin_test.cpp`): input ID
10 and old-ID preservation; a ±5 V saw at both ranges against the DC blocker's
response; GATE patched low is silent and high sounds; the gate-free note
follows PITCH and a patched GATE holds it; range option JSON, including a
2.5-shaped patch without it and Initialize; a 2.5-shaped patch loads with its
parameters and sound unchanged; one-sample feedback loops through LP4, DIST,
GAIN and COMB do not fault. ByteBusLab's `tests/lookup_test.cpp` checks the
compiler's rejection.

Polyphonic step, still to be built: voice count from the AUDIO IN cable and
mono broadcast.

Workstation: Machinemodule TRACKS → Vastish AUDIO IN (poly) → Spoon Bender,
and the modular through an ES-6 → Vastish, with what was heard noted in the PR.
