VastishLab ports the behavior and programs of `pd/vastish` in this repository.
No Kurzweil firmware, sample ROM, manufacturer DSP code, or Pd runtime is included.

The cosine table, resonant filter, one-pole filters, noise and per-note random generators, cubic delay
interpolation and nonlinear ladder equations in `src/engine/Dsp.hpp` are adapted
from Pure Data 0.55-2 by Miller Puckette and contributors, principally
`src/d_osc.c`, `src/d_osc.h`, `src/d_filter.c`, `src/d_delay.c`, `src/x_misc.c`, and
`extra/bob~/bob~.c`. Original copyrights include 1997–1999, 1997–2024 and 2015
Miller Puckette. The applicable BSD-style license and disclaimer are reproduced
in `docs/PURE-DATA-LICENSE.txt`. Reference tests compile these upstream files
from a user-supplied checkout; those files are not bundled.

Panel lettering uses outlines from DejaVu Sans. The font license is reproduced
in `docs/DEJAVU-LICENSE.txt`. Font data is not bundled as a runtime dependency.

The plugin links against the VCV Rack SDK. VCV Rack and its standard UI assets
are supplied by the host installation, under their respective licenses.
