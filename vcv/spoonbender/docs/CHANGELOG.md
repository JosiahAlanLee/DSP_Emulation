# Spoon Bender change history

## 2.2.1

- Sample-rate changes clear the wet-path DC blockers and the measured clock period, so old state cannot produce a transient or keep TIME synced to an obsolete period.
- Initialize restores the ESP rate and memory budget defaults, clears clock and capture triggers, and empties capture history and the published capture slot as well as effects tails.

## 2.2.0

Captures are stereo: OUT L and OUT R after all mixes, each 16-bit. OTIS 2.7.0 and later read both channels through a stereo extension of the sample link; earlier OTIS versions still read a mono mix, (L + R) / 2, which equals the earlier mono capture whenever the channels match and is within one 16-bit step otherwise. A capture with identical channels is shared as mono. The capture history uses twice the memory: about 7.7 MB at 48 kHz for the 20-second maximum.

## 2.1.1

Added the shared panel mark. No DSP changes.

**2.1.0**: version numbers now start with 2. Rack 2 refuses to load any plugin
whose major version isn't 2. As 1.1.0, Spoon Bender installed but never
appeared in the module browser. Releases 1.0.0–1.1.0 below were never loadable
in Rack.

**1.1.0**: renamed from ESPish to Spoon Bender. The plugin slug is now
`SpoonBenderLab` and the module slug `SpoonBender`. Patches saved with ESPish
won't find the module; nothing was published under the old name. OTIS 2.5.1
looks for the new name. No DSP changes.

**1.0.1**: fixes from GPT's review.

- **ESP dry/wet comb filter:** the dry parts of the mixes used to go through
  the converters, and their latency combed against the undelayed dry path:
  −41.5 dB at 300 Hz with MIX at 50%. The dry parts are now added at Rack's
  rate.
- **SHIFT typed entry:** typed values are now in Hz. Typing 1 used to give
  2 kHz. With CLOCK patched, TIME takes a typed clock ratio.
- **Quiet ESP frozen tails:** they no longer grow. Before, they gained 7.4 dB
  over 30 s at low level.
- **Capture and sample-rate changes:** collecting a capture is now
  synchronised with sample-rate changes, and each capture keeps the rate it was
  recorded at.
