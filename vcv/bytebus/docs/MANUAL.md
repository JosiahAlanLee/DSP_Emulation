# Clocked Register

**ByteBusLab · 12 HP · one 8-bit word of clocked state**

The register samples BUS IN on its selected clock, holds that word, and emits
it through BUS OUT with a word-clock pulse. RESET loads the chosen seed word
and publishes it to downstream clocked modules. All processing is on the audio
thread, without allocation or locks. This implements the byte-bus spec in
[`docs/BYTE_BUS_SPEC.md`](../../../docs/BYTE_BUS_SPEC.md).

## First patch

1. Add **Clocked Register** from **ByteBusLab**.
2. Connect up to eight gates to Core **Merge**, then Merge to **BUS IN**.
   Bit 0 is Merge input 1; bit 7 is input 8. A ninth channel is an optional clock.
3. Leave **SOURCE** at **Auto**. With no external or bus clock, RATE supplies
   the clock, initially 8 Hz. Connect BUS OUT to Core **Split** to see its bits.
4. Move the input gates: the LEDs and hex display change only at clock edges.
5. Choose **RESET WORD** (0–255) and press **LOAD**, or trigger RESET. The seed
   appears immediately, is published with a clock pulse, and survives until
   the next acquisition edge. With no BUS IN cable the held word is repeated.

The display is conventional hexadecimal: 00–FF. The LEDs read bit 7 at the left
to bit 0 at the right. Cable channel order remains LSB first.

## Controls and ports

| Item | Behavior |
|---|---|
| SOURCE | Snapped knob: Auto, CLOCK jack, Bus CLK, Internal, Engine rate. The parameter tooltip names the selection. |
| RATE | Internal clock, 0.125–2048 Hz. Used only when Internal owns the clock. |
| RESET WORD | Integer 0–255. Changing the knob does not alter the held word until LOAD/RESET. |
| LOAD | Loads RESET WORD, restarts the internal clock phase and publishes the seed. |
| BUS IN | 8 data channels, or 9 with CLK; fewer data channels mean a smaller word, never mono broadcast. Reserved channels above CLK are ignored. |
| CLOCK | Monophonic clock; rising at 1 V, falling at 0.1 V. |
| RESET | Monophonic trigger, same thresholds; loads on its rising edge. A held reset does not reload every sample. |
| BUS OUT | 9 channels, exact 0/10 V bits, channel 9 the word clock. Engine mode emits 8 channels. |
| CLK OUT | The same one-engine-sample publication pulses as BUS OUT's CLK. Always low in Engine mode. |

## Clock ownership

**Auto** selects CLOCK if that jack is connected; otherwise BUS IN's CLK if
the bus has at least nine channels; otherwise the internal Hz clock.
Selection is based on connection, never on how recently a clock edge arrived.
A connected but stopped clock freezes acquisition; it never silently starts the
internal clock. **CLOCK jack** and **Bus CLK** force those sources; if absent
they remain stopped. **Internal** ignores other clock sources. **Engine rate**
explicitly acquires every engine sample and emits no clock channel, since an
edge every sample cannot be represented with a required high/low gap.

Switching to a different effective clock source restarts the internal phase. All
external edge detectors continue tracking while unselected, so selecting an
already-high, already-tracked clock does not invent a new edge. During normal
running, a newly connected high jack clock, or a bus changing to clocked mode
with CLK high, can count as an edge. The first sample after a fresh start,
Initialize or restore instead publishes the held word; it consumes that initial
high level without acquiring.

With a local CLOCK or Internal selected, the register samples BUS IN's current
decoded data lines directly, even if that input also carries a stopped clock.
This deliberately reclocks the data, rather than applying a second upstream
clock latch first. Bus CLK mode samples the data on the bus's rising edge.

## Clock and reset timing

- Acquisition samples the word and emits a one-engine-sample high word clock
  in the same process call. Normal clock duty cycles are regenerated as short
  publication strobes; they are not passed through unchanged.
- Every high strobe is followed by at least one low sample. The fastest valid
  clock is half the engine rate. RATE is additionally limited to the panel's
  range. Rack cables add one engine sample per module hop.
- RESET takes priority over a simultaneous acquisition edge. It loads the seed
  and restarts internal phase at zero. It does not sample feedback on that edge.
- A new instance or a restored patch also publishes its held word first.
  On the first sample after Initialize or a restore, the held word is
  published first, and a clock that is already high is not counted as an
  edge; only its next rising edge counts. Every ByteBusLab module that owns
  its clock follows this rule, Lattice's V CLOCK included.
- Reset/load emits a seed strobe even when the selected clock is stopped. This
  is how a downstream clocked reader receives the initial word. If the previous
  output strobe was high, a low sample is inserted first and the seed strobe
  occurs on the following sample. The word itself has already been loaded.
- Acquisition is suppressed during seed publication and the following low
  sample. Clock edges during that short window are discarded, not queued.
- Holding RESET high does not block subsequent clock edges. Only its rising
  edge loads. Releasing it and triggering again loads again.

For a feedback loop, the clock period must exceed the complete cable and
processing path's settling time. More modules in a loop mean a longer minimum
period. The register supplies an explicit sampling point; it does not eliminate
propagation delay. One-sample CLK OUT pulses are Rack signals, not a promise
that a physical interface or hardware device will capture them.

## Feedback with today's Vastish

This does not require the proposed Vastish BUS integration or BIT mode:

1. Register BUS OUT → Vastish AUX1.
2. Supply an eight-channel cable to Vastish V/OCT, for example Core Merge with
   all eight inputs connected to 0 V. This activates all eight FUN lanes.
   No gate is needed. Merely connecting AUX does not establish polyphony in
   the current Vastish.
3. Send eight constant 10 V gates through Merge → AUX2. This is the XOR mask FF.
4. FUN1: A = AUX1, B = AUX2, EQ = ABS(A−B). FUN1 OUT → Register BUS IN.
5. Use the register's **Internal** clock, or a clock patched to CLOCK with
   **SOURCE = CLOCK jack**. Its input is now the unclocked FUN bus.
6. RESET WORD = 165 (A5). Press LOAD: words alternate A5, 5A, A5, 5A on
   subsequent acquisitions. A mask with fewer set bits toggles only those bits.

These inputs and that equation always give canonical 0/1 results. Arbitrary
arithmetic FUN chains are not automatically Boolean; the proposed BIT mode is
separate work. Ordinary Merge/Split and bit logic can replace Vastish here.

## Saving and reset

Rack saves controls and the held word. Reload publishes the saved word; it does
not resume the fractional internal clock phase. Trigger RESET for a repeatable
starting word and internal phase. Rack's module Initialize command restores
default controls and word 0. No DAC output is provided: binary 00 is zero as a
word, but a separate offset-binary DAC interprets it as full negative voltage.

## Validation

Portable tests exhaust all 256 word conversions and cover Schmitt hysteresis,
non-finite values, missing channels, connection/mode transitions, raw reclocking,
clock ownership, frozen clocks, reset priority, seed publication, output gaps
and internal timing at 44.1/48/96 kHz. Rack API tests cover real ports, JSON
restore and an actual Register/Vastish FUN feedback loop with delayed cables,
random process order and three seed words across those sample rates.

The prototype is outside the release bundles. Software tests do not establish
physical clock transport or live workstation behavior.

## First sample after Initialize or restore

A fresh instance or Initialize starts at 00; restore publishes the saved
word first. BUS IN can already carry raw data or a high bus CLK: neither
replaces that word on its initial publication. RESET or LOAD already high
loads RESET WORD once instead; holding it high allows later acquisitions.
The shared bus reader itself still latches an initially high bus CLK; that
input latch is distinct from permission to acquire this register.
