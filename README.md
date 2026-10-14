# DSP_Emulation

Recreations of hardware synth architectures in software environments.

| Path | What | Status |
|---|---|---|
| [`pd/vastish`](pd/vastish) | Kurzweil K2000/K2500 VAST-style 8-voice synth in vanilla Pure Data (0.52+) | working; tested headless |
| [`vcv/vastish`](vcv/vastish) | Native C++ port of Pd Vastish for VCV Rack 2; eight voices, six algorithms, fourteen DSP functions | Linux build; DSP and Rack API tested; live GUI/audio pending |
| [`vcv/otislab`](vcv/otislab) | EPS-16+-style sample player for VCV Rack 2, built on MAME-derived ES5505 (OTIS) playback arithmetic, with experimental window/frame scanning and loop reworking | prototype; engine bit-exact vs reference core |

Kurzweil, VAST, Ensoniq and EPS are their owners' trademarks. These are independent emulations, not affiliated with or derived from manufacturer code, firmware or samples. OtisLab's playback arithmetic is adapted from MAME (BSD-3-Clause, Aaron Giles); see its THIRD_PARTY_NOTICES.
