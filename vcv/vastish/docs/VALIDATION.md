# Validation — native Vastish 2.0.0

Built 2026-10-14 with GCC 13.3, Rack SDK 2.6.4 Linux x64, Ubuntu 24.04.
Port based on DSP_Emulation commit `85c829141249de7fac947f6468f125c854f3748f`.
Pd reference: Pure Data tag 0.55-2, commit
`68a30a3e2672d0d57baff7d862d6b611c79d53ec`.

## Independent Pd arithmetic comparison

Reference wrappers compile the unmodified upstream C files and call their actual
perform routines. Inputs comprise 8,192 deterministic noise samples, varying
cutoff/resonance, cosine phases across ±20 cycles, and Q=0.5/2.5/40. Tested at
44.1, 48 and 96 kHz. Comb reference uses the actual 16-sample read/write feedback
schedule. Maximum absolute errors across these tests:

| Primitive | Maximum error |
|---|---:|
| bob~ two-substep RK4 ladder | 0 |
| cos~ 2048-point table | 0 |
| hip~ 5 Hz DC blocker | 0 |
| lop~ signal cutoff | 2.38419e-7 |
| vcf~ real and imaginary outputs | 2.68221e-7 |
| delread4~ feedback comb | 3.46899e-5 |

These are finite tested buffers, not a universal bit-exact claim. Float operation
ordering, interpolation rounding and host timing can change results. Complete
results are in `pd-reference-results.txt`.

## Full-program renders

Both versions render the event list from `pd/vastish/tools/mkseq.py` at 44.1 kHz,
including the mod-wheel ramps. Native rendering uses the Pd poly allocator's
oldest-free-voice ordering; Rack itself accepts eight supplied CV voice channels.
Pd ran headless in real time to let its asynchronous WAV writer finish, with a
10-second final wait. All files were checked after rendering completed.

| Program | Pd RMS | Native RMS | Difference |
|---|---:|---:|---:|
| Ladder Bass | 0.084673 | 0.084601 | -0.086% |
| Ring Bell | 0.109122 | 0.109076 | -0.042% |
| Fold Pad | 0.061110 | 0.061110 | -0.001% |
| Crush Lead | 0.133494 | 0.133265 | -0.172% |
| Comb Pluck | 0.038083 | 0.040955 | +7.542% |

RMS combines both channels over the full recording. The first four programs
match overall levels within 0.2%. Comb Pluck differs by approximately 7.5%,
with different deterministic noise seeds and host scheduling; this is not a
waveform match. RAND panning can differ while stereo energy remains equal.
Recordings differ by at most 32 frames because of Pd block scheduling. There
were no nonfinite samples or engine fault resets in the musical renders.
Measured levels and frame counts are in `program-comparison.json`.

## Engine, adapter and Rack API

- Six routing graphs checked against known constant-gain paths; envelope attack,
  decay and release timing checked independently.
- All six programs rendered with eight voices at 44.1/48/96 kHz. Each DSP function
  exercised at parameter endpoints. Steady-state processing allocated no memory.
- FUN dependency order, unsupported recursive-edge containment, gate retrigger,
  audio-thread program loading, held-gate Panic and broad editor extremes checked.
- Actual `plugin.so` dynamically loaded against `libRack.so`: module creation,
  eight-channel pitch/gate, mono VEL/MOD broadcasting, disconnect release, full
  Rack module JSON save/restore, and malformed CV containment passed.
- Core AddressSanitizer and UndefinedBehaviorSanitizer checks passed on the engine
  test. LeakSanitizer was disabled because the sandbox does not permit its process
  inspection; no leak claim is made.
- Outlined panel rasterized and visually inspected. This is a layout illustration;
  selector mouse interaction and a live Rack window have not been tested.

The standalone engine measured approximately 0.05–0.17 CPU seconds per second of
8-voice audio at 48 kHz on this host; the ladder program is the heaviest. These
numbers exclude Rack/device overhead and are not a latency guarantee. Logs are
in `engine-results.txt`, `adapter-results.txt`, and `rack-api-results.txt`.

## Remaining live checks

A live Rack GUI, real-time callback load, your Ubuntu/PipeWire/Digi003 ADAT path,
Expert Sleepers calibration and hardware CV levels remain untested. This Linux
binary requires glibc 2.35+. Windows and macOS builds remain untested. Changing
sample rate clears comb buffers. Existing ES5505/OTIS code is unchanged.

The source port retains oscillator aliasing, squared amplitude envelopes,
quadrature filter behavior, equal-power pan and Pd mix coefficients. It does not
add a Kurzweil sample engine, manufacturer DSP programs, or a firmware emulator.
