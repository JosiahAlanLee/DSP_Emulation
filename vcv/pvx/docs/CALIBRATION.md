# Behavioral model and future calibration

The published design uses programmable-op-amp behavior as current-controlled integration rather than conventional external filter capacitors. It offers low-pass and band-pass outputs and aggressive resonance. PVX approximates those properties with a saturating state-variable model; its oscillator, envelopes, noise, overdrive and calibration constants are original software choices.

Sources reviewed for circuit behavior:
- https://github.com/clarionut/Polivoks_filter — builder's schematic, programmable-op-amp behavior and comparison with an original unit.
- https://northcoastsynthesis.com/news/modular-synthesis-intro-part-8-statevariable-filters/ — filter designer's explanation of the current-controlled integrators and their nonideal behavior.
- https://www.tubeohm.com/wa_files/shruthi-polivoks_diy-manual.pdf — hardware builder's calibration discussion; do not assume analog self-oscillation tracks equal temperament accurately.

No recordings, sweeps, component measurements or calibration data from Josiah's instrument have been used. No claim of hardware-equivalent aliasing, tuning, distortion or response is made.

`dspemu::pvx::FilterCalibration` collects provisional damping, integrator saturation, input drive, cutoff-dependent drive and oscillator-startup constants. `Parameters` sets cutoff, resonance, mode, filter envelope, oscillator/noise levels and drive; `Input.external` / `Parameters.extLevel` permit a known test signal instead of the oscillators. Raw LP/BP outputs allow response measurement before the VCA/overdrive. Keep the gate high, oscillator/noise levels zero and filter-envelope depth zero for a steady filter sweep. Use a low external amplitude for linear response, then sweep amplitude for nonlinear behavior.

When Josiah is ready to measure the hardware, record sample rate, input/output gain, mode, cutoff position, resonance, envelope settings and power conditions. Useful fixtures:

1. Low-level logarithmic sine sweeps at several cutoff positions in LP and BP, resonance minimum/middle/high. Fit cutoff mapping and small-signal damping first.
2. No-input self-oscillation at the same positions: frequency, startup time, amplitude and spectrum versus resonance. Fit tuning separately from loop gain and amplitude limiting.
3. A fixed sine and saw at several amplitudes through the same settings: spectral levels and cutoff-dependent overload. Fit saturation/drive after the linear response.
4. Envelope-controlled sweeps and transients after static matching, keeping oscillator tuning and measurement gain fixed.

Store recordings as new read-only inputs when authorized; fit a new named calibration profile and retain the current generic profile for comparison. Separate hardware measurements from digital test results in every report. Nothing here requires connecting physical gear for this job.
