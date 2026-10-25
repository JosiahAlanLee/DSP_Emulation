# Converter

ByteBusLab 2.2, 14 HP. Independent ADC and DAC paths connect ordinary Rack
signals to the [byte bus](../../../docs/BYTE_BUS_SPEC.md).

## Conversion

ADC reads SIGNAL channel 1. Gain (-4..4) and offset (-10..10 V) apply before
conversion. Select +/-5 or +/-10 V range:
`word = fromBipolar((voltage*gain + offset)/range)`.
Unpatched SIGNAL is 0 V, so offset can generate a constant byte. Non-finite
signal produces neutral 80. CLIP lights for invalid signal or calibrated values
outside the nominal selected range. Normal endpoint quantization is allowed.

DAC decodes BUS IN to `(word-128)*range/128`, then applies its own gain/offset.
80 is exactly zero; FF is +4.9609375 or +9.921875 V before trims. DAC trims
apply continuously while its word holds. Disconnecting BUS IN resets its word
to 80. Ranges and trims are independent; negative gain inverts, and output trims
can exceed the nominal range. No dither, normalization or filtering is applied.
Two hex displays show the ADC/DAC words.

## Clocks

ADC's saved clock policy is Auto (CLOCK when patched, otherwise engine rate),
Engine (ignore CLOCK) or Jack (hold indefinitely if CLOCK stops or is absent).
Clocked ADC emits nine channels and holds the calibrated value between edges;
Engine emits eight and converts every sample. Initial ADC word is 80. CLK OUT
matches the ninth lane, or zero in Engine. Patching a high clock counts as an
edge. Changing between clocked/engine modes rearms its acquisition detector.

DAC's independent saved policy is Follow bus (default), Engine (current raw
bits every sample) or Jack (sample current raw bits on CLOCK edges). Jack does
not add an upstream-clock latch. Follow uses the Reader's initial clocked word
00 until its first rising edge; Jack starts at 80 and rearms when selected.
Disconnected BUS IN resets to neutral regardless of policy.

CLOCK uses the shared 1 V/0.1 V Schmitt thresholds. There is no timeout or
internal oscillator. Reset clears latches/detectors; patch saves preserve the
parameters, not running capture state. Each cable hop costs one engine sample;
independent jack clocks require the user to account for cable latency.

## First patch

SIGNAL -> ADC BUS -> Lookup BUS IN. Lookup BUS OUT -> Converter BUS IN.
DAC OUT -> mixer. Start with default ranges/trims and no CLOCK. Add CLOCK for
held conversion; invert ADC gain to reverse scan direction. The ADC bus can
also select OTIS frames while the independent DAC decodes another bus.

The included **examples/byte-bus-io.vcv** uses two rows of 98 HP. Install
ByteBusLab 2.2, Vastish 2.5 and OTIS 2.8; choose an audio device and press OTIS
PLAY. A gate-free Vastish saw drives Converter's DAC, its ordinary voltage
feeds the ADC, Register samples at 4 Hz, a second Vastish shapes the held
word, and Lookup's Walsh permutation chooses frames in an embedded original
stereo bank. Change the converter trims, Register RATE or either transform.
Rebuild with tools/make_io_patch.py after building the standalone compiler.
