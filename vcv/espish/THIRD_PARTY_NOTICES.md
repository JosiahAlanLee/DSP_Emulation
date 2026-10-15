# Provenance and third-party notices

ESPish models the *arithmetic* of the Ensoniq ES5510 ("ESP") as documented by
MAME's ES5510 core (`src/devices/cpu/es5510/es5510.cpp`, `es5510.h`; BSD-3-Clause,
Christian Brunschen), at commit 016b077915e5e12349e5d590fbbe47595717d27b:
24-bit saturating ALU, 48-bit multiply-accumulate read back as 24 bits, and 16-bit
delay-memory writes rounded toward minus infinity. `tests/engine_test.cpp`
reproduces short excerpts of those routines to check parity; the complete BSD
notice is in LICENSE. ESPish does not execute ES5510 programs and contains no
Ensoniq firmware, effects microcode, or samples. It is independent and not
endorsed by Ensoniq or MAMEdev.

The Hilbert-transformer coefficients in `src/engine/Fx.hpp` are Olli Niemitalo's
published all-pass design for a 90-degree phase-difference network (public
domain).

The rate converter follows the design of OtisLab's output converter in this
repository (64-tap, 1024-phase Blackman-windowed sinc).

Panel lettering is outlined from DejaVu Sans. DejaVu font licensing:
https://dejavu-fonts.github.io/License.html (see docs/DEJAVU-LICENSE.txt).

VCV Rack provides the plugin API, standard controls and runtime. Build against
the separately downloaded Rack SDK and follow its license and plugin exception.
