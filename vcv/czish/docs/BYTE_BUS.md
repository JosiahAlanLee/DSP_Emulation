# Encoded-word ByteBus — CZishLab initial contract

Both modules provide **XOR BUS** and **BUS OUT** using the
[Harriot convention](https://github.com/JosiahAlanLee/DSP_Emulation/blob/main/docs/BYTE_BUS_SPEC.md): data bits are 0/10 V,
least-significant bit first; a ninth channel, when present, is CLK.

## What the bits mean

BUS OUT is the high eight bits of the **encoded device word after XOR**, before
noise, analog reconstruction, tone and output gain. DAC Stage sends channel 1's word;
CZish sends its mono chip sum's word. It is not a polyphonic audio bus.

| Profile | Native word | BUS OUT |
|---|---|---|
| YM FP | 13 bits: 3 exponent, sign-inverted 10-bit mantissa | bits 12–5: exponent, sign and the high mantissa bits |
| CZ model | 14 bits: 2 range bits and a signed 12-bit mantissa | bits 13–6: range and the high mantissa bits |
| Linear, 8–16 bits | Signed two's-complement word | its most significant 8 bits |
| Linear, 4–7 bits | Signed two's-complement word | left-aligned; unused low bus bits are zero |

Both CZ compander candidates publish their selected range and quantized mantissa.
The CZ word describes this project's provisional model; it is not asserted to
be a hardware CZ word. For YM positive input overflow, ymfm's specified
round-trip returns +32767 even though the maximum encoded word decodes to
+32704. With no XOR we preserve that behavior; when XOR is applied, the modified
word is decoded.

## Manipulating a word

**XOR BUS** is a mask, not a new audio input. Its eight bits are XORed with the
high eight bits of each encoded device word before decoding. The word's
remaining low bits stay as encoded. An unconnected bus or `00` leaves the
converter unchanged. For a Linear word shorter than eight bits, the mask's
unused low bits have no effect. A CZ range code of 3 can be made by XOR; this
model decodes it as range 2.

An eight-channel-or-shorter input is read every Rack sample. A nine-channel
input updates its mask on CLK's rising edge and holds it between edges. The
mask applies to all DAC Stage audio channels. There is no mono broadcast: a
one-channel cable is mask bit 0, not a 0–10 V control value.

This is a creative modification of the digital word, not the behavior of an
unmodified Yamaha or Casio converter. Flipping sign or exponent bits can
produce a sudden jump or sustained DC. Begin at a low level and patch through
a suitable output stage.

## Publication clock

BUS OUT always has nine channels. The device may generate more than one word
per Rack sample; moreover, Harriot requires at least one low sample between
clock pulses. It is impossible to publish every word when the device runs
faster than half the Rack sample rate. The tap therefore publishes every
`N`th device word, where `N = max(1, ceil(2 * device_rate / rack_rate))`.
The **BUS / DIV** readout shows the last published byte and N. At 48 kHz Rack
rate, for example, YM FP at about 55.93 kHz uses N=3, producing about 18.64 kHz
of bus publications; CZ at 40 kHz uses N=2, producing 20 kHz.

Data and CLK are emitted in the same Rack sample, and each high is followed by
a low sample. If rapidly changing RATE creates a new publication while the
mandatory low gap is in progress, the most recent pending word is emitted at
the next permitted sample. Intermediate words can therefore be skipped. The
tap does not pretend to export every chip sample at high rates.

The input reader observes the incoming clock independently of the converter's
device clock. A connected, stopped clock holds the last XOR mask. Each Rack
cable hop adds one sample; feedback through Register/Lookup therefore has
explicit delay. BUS OUT precedes CLEAN's output-filter delay and should not be
used as an exact clock for the audible output.
