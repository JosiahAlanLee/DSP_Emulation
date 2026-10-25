# OTIS frame bus and Lookup authoring

Contract v1, OtisLab 2.8. These are original LAB extensions. The shared
[byte-bus protocol](../../../docs/BYTE_BUS_SPEC.md) defines the cable, levels
and clock. No manufacturer waveform tables or firmware are inputs.

## Frame selection

**FRAME BUS** is one byte, not eight voices. In **FRM** mode it replaces POSITION
and POS CV for every voice. START, END and SIZE still define each voice's region
and frame length. SEG and SCAN ignore the bus. Disconnecting restores POSITION
and POS CV on the next engine sample. The bus does not trigger notes, establish
polyphony, change pitch or clock the sample engine.

For N complete frames, byte b selects `floor((b*(N-1)+127)/255)`. Thus 00 selects
the first and FF the last; above 256 frames, some frames cannot be selected.
Trailing incomplete frames are omitted. A region smaller than SIZE becomes one
frame, as in existing FRM playback. Each voice maps the byte over its own region.

Eight or fewer cable channels update every engine sample. Nine or more latch
the byte on channel 9's rising edge and hold it indefinitely between edges.
An initially high clock counts as an edge. The held *byte* survives edits to
START/END/SIZE; those controls can still change the resulting window. Rack adds
one engine sample per cable hop. RESET on an upstream register publishes its
seed using the register's normal contract. OTIS adds no clock fallback.

Frame moves retain the playback offset and fractional position within a frame.
**Smooth FRAME BUS changes (2 ms)** defaults on and saves in the patch. When
moving between equal-length frames, the old and new frames are read at the
same offset and blended before the chip filters. Both stereo channels use the
same blend. The fade length is rounded to chip ticks using the effective chip
rate when the move begins. During a fade, requests coalesce to the latest
selection; that selection starts when the current fade finishes. Smoothing can
therefore delay a selection at fast clocks. Disable it for immediate switching.
Changing frame length, leaving FRM, disconnecting, or disabling smoothing
cancels a fade. These changes, internal frame seams and hard switching can
still click. This is not a band-limiting or click-free guarantee.

## Export a frame to Lookup

Choose **Export selected frame to Lookup** in OTIS's menu, then Left, Right, or
Mono mix. Right requires a stereo sample; Mono mix uses `(L+R)/2` before byte
quantization and uses the left data for a mono sample. FRM mode and a finished
sample are required. The target is voice 1's most recently processed requested
frame, including its START/END/SIZE and held bus selection, even during a fade.

Export snapshots stored PCM off the audio thread. A sample revision change
between selecting and copying rejects the export; retry after REWORK or recording
finishes. Recording and pending capture publication also reject export. No chip
filter, playback pitch, output gain, normalization or DC removal is baked in.

For a frame of L samples, output address i reads periodic position `i*L/256`,
linearly interpolating adjacent PCM samples and wrapping the neighbor at the
frame end. Quantization is `clamp(floor(128 + pcm/256 + 0.5), 0, 255)` for signed
16-bit PCM. This is a deliberate 8-bit, 256-point copy; long or discontinuous
frames may alias and need not be single-cycle waves.

The JSON is `ByteBusLab.LookupTable`, version 1, with 256 byte values and their
FNV-1a fingerprint. Its source records the OTIS exporter version, source name,
sample revision/rate, frame bounds, chosen channel and resampling rule. In
Lookup choose **Load original Lookup table (.json)**; it selects Custom / Shape
32. Lookup embeds the table and provenance in its own saved patch. Exports are
snapshots: later OTIS recordings or REWORK results require another export.

This first implementation exports one selected frame. It does not transfer a
multi-frame bank or add an external phase playback mode.

## First patch

Load `examples/OTIS-byte-frames.vcv`, choose an audio device, press OTIS PLAY
and the Register RESET button. Register feeds Lookup's byte inversion back to
itself, alternately selecting frames 5 and 2 of eight original stereo waves at
2 Hz. OTIS continues its own phase through the changes. Turn off smooth frame
changes to compare hard switching. The patch contains its sample and connects
L/R to a Core Audio-2 module; no external sample file is needed.
