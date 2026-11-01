# PVX acceptance, 2026-10-30

Accepted engine: DeepSeek's behavioral filter/voice, followed by reviewed bipolar-noise, pulse-edge, fractional-sync and retired-voice corrections; a second scoped revision stabilizes high-cutoff low-rate modes and replaces boxcar decimation with the repository's windowed-sinc FIR. Codex added final post-VCA subnormal flushing, independent regression/spectral checks and the Rack integration. Later CPU-approximation experiments on `deepseek/pvx-industrial-dsp` are not this accepted engine.

All figures below are digital/software measurements. No hardware recordings, component measurements, acoustic note-recognition test or listening review is claimed.

## Commands and results

- `make -C tests test` (`g++ -std=c++17 -O3 -Wall -Wextra`, no fast-math): **3,875 broad checks + 208,066 focused checks, zero failures**; three high-cutoff regressions and a tiny-release regression also pass. These cover 44.1/48/96 kHz, resonance/cutoff/drive sweeps and 1×/2×/4×; finite bounded state, no internal denormals; AR/ADSR timing, VCO tuning, duty, sync, response, self-oscillation and channel retirement.
- `make -C tests module-check RACK_DIR=/home/echevarian/dev/Rack-SDK`: **PASS real SDK**, eight poly voices, mono-gate broadcast, 16-channel input capped to eight, shrink/re-expand, full resonance, invalid CV and engine reset.
- `make RACK_DIR=/home/echevarian/dev/Rack-SDK`: **PASS** installed SDK. Diagnostic private Rack capture successfully loads PVXLab 2.0.0 and both PVX module instances. No installation into the user's Rack folders.
- A controlled hard-sync sample now matches analytic phase to **3.4×10⁻⁸ cycles**, versus the draft's 0.119-cycle error. A 49-pair phase grid has zero mismatches. Pulse duty matches 0.10/0.25/0.50/0.75/0.90 at 100 Hz and 1 kHz. Million-sample noise mean **−0.000102**, range approximately [−1,1).
- Linear response fixture (not a hardware match), 1 kHz natural frequency: LP approximately **−6.03 dB at 1 kHz**; BP at resonance 0.8 approximately **+24.52 dB**, analytic +24.73 dB. The two-pole zero-resonance fixture has Q≈0.5, so its natural frequency is not the −3 dB point. See printed response tables in core_test.
- Full-resonance self-oscillation is stable and bounded. Requested 1 kHz gives approximately **987 Hz** at 44.1/48/96 kHz (about −23 cents). Full 100 Hz–16 kHz tables are printed by the core test. This is characterized tracking, not an equal-temperament tuning guarantee.

## Independent spectral measurement

48 kHz host, one-second warm-up, 65,536 steady samples, four-term Blackman-Harris window. Remove pulse DC and exclude only true harmonics below Nyquist, ±8 bins. High-frequency test: 5,432 Hz saw/pulse/hard-sync (+7 semitones), requested cutoff 20 kHz, low oscillator levels; driven sine: 997 Hz, drive 1. Measured nonharmonic spur energy relative to intended harmonics:

| Path | 4× before FIR/stability revision | Accepted 4× |
|---|---:|---:|
| Saw | −24.57 dB | −51.75 dB |
| Pulse | −25.31 dB | −51.31 dB |
| Hard-sync pulse | −21.49 dB | −51.20 dB |
| Driven sine | −94.92 dB | −94.92 dB |

The metric includes nonharmonic numerical artifacts and foldback; it cannot identify aliases coincident with legitimate harmonics, and is not a universal bound across every pitch, control or transient. Fixed-length FIR stopband/transition and rate-dependent bandwidth mean the metric need not improve monotonically at each oversampling setting. The former 1× Nyquist limit cycle is absent; high-cutoff regression preserves the requested fundamental with low internal swing in all three modes. Raw measurements and reproducible harness are in `shared/codex/pvx-industrial-20261030/spectrum/` and the adjacent render_spectral.cpp/analyze_spectral.py.

## CPU and latency

Ryzen 5 5600X, g++ `-O3` without fast-math; two seconds per configuration after warm-up, consumed output checksum, process CPU time and wall time recorded separately. Busy patch: two pulse oscillators, sync, noise, resonant filter and overdrive. At 48 kHz/4×: **734.9 ns per sample / 3.53% of one core for one voice**; **738.4 ns per voice-sample / 28.35% for eight voices**. Eight voices: 25.80% at 44.1 kHz/4× and 55.16% at 96 kHz/4×. These exclude Rack/UI/JACK overhead and are measurements of this patch, not deadlines or minimum hardware guarantees. An optional <10%-core target was not met; no unvalidated approximation was substituted to claim it.

FIR delay is approximately 30 internal samples (0.156 ms at 48 kHz/4×), distinct from oscillator BLEP, envelope/filter phase and host audio buffering. Sinc tables are prewarmed in the engine constructor. The supplied capture rig uses a conservative 2,048-frame JACK buffer; that host latency is not the portable engine's FIR delay.

## Remaining calibration limits

All filter constants are provisional. The shared minimum-phase BLEP can introduce frequency-dependent saw DC bias; pulse DC also varies intentionally with duty. Spectral analysis reports/removes DC separately. Published circuit descriptions motivated the topology and nonlinear behavior; future hardware sweeps must establish any actual match. See CALIBRATION.md for fixtures and the parameter hook.
