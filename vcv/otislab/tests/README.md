# Test reproduction

`make -C tests test` builds/runs the portable engine tests. Run from the project
root. `make -C tests sanitize` also builds and executes ASan/UBSan checks.

For Linux Rack API validation, first build the plugin, then:

```sh
g++ -std=c++17 -O2 \
  -I/path/to/Rack-SDK/include -I/path/to/Rack-SDK/dep/include \
  tests/plugin_test.cpp -L/path/to/Rack-SDK -lRack -ldl \
  -o tests/build/plugin_test
LD_LIBRARY_PATH=/path/to/Rack-SDK tests/build/plugin_test
```

`render.cpp` produces two seconds of looping mono audio from the same engine:

```sh
tests/build/render examples/bright-demo.wav output.wav -12 0 48000
```

Arguments are input, output, semitone offset, clock mode (0/1/2), host rate.
`spectral_test.py` requires numpy/scipy and runs 12 pitch-alias checks, then
creates the audition WAV. `reference_compare.cpp` requires a separate checkout
of mardlib's EPS emulator. It is not part of the distributed plugin build.
See its header for compiler commands; it uses only reference code in that
separate checkout and does not distribute it.

`make -C tests test` also runs `snapshot_test`: it pins a finished sample during
playback and checks deferred capture publication, queued starts and cancellation.
Its concurrent producer/reader checks complete generation consistency across
200 takes. Run `make -C tests thread-sanitize` to exercise it under ThreadSanitizer.
All snapshot readers run off the audio thread; processing remains a single-writer
operation. Module control/import callers retain their existing processing lock.

`stereo_test` checks 2.6.0 stereo samples: in 60 random scenarios across clock modes, filters,
loops, windows, LAB clock and interpolation, a stereo pair's L and R outputs equal two mono engines
playing each channel, bit for bit; stereo takes equal two mono takes for every record source; voice
limits and capacity halve; stereo, mono and three-channel WAV loading; REWORK on both channels,
directly and through the worker. `stereo_plugin_test.cpp` (build like `plugin_test.cpp`) checks the
compiled module: stereo patch data round trip, L/R against per-channel mono modules, old patches
staying mono, the mono mix shared with Vastish and a stereo take from AUDIO R.

`lab_test` covers the 2.2.0 LAB controls (clock, interpolation bits, resample source, per-voice START/END) and runs as part of `make -C tests test`.


`window_test` checks fixed window lengths, whole-frame selection, phase
preservation, interpolation at frame edges, reverse/ping-pong behavior, worker
publication, stale-load/capture rejection, cancellation, tiny/invalid windows,
and zero processing-thread allocations. `mutation_concurrency_test` checks
coherent snapshots during processing, rework and sample replacement. Both run
under `make -C tests test`; `window-sanitize` runs ASan/UBSan and
`thread-sanitize` also runs the new concurrent worker test.

`window_plugin_test.cpp` dynamically loads the real plugin, verifies poly frame
CV against known DC levels, triggers background processing, checks region-limited
PCM changes, and saves/restores the complete Rack module. Compile like
`plugin_test.cpp` above, adding `-pthread -Wl,--allow-shlib-undefined`.

`legacy_trace.cpp` builds against either the current engine or a supplied
`OTIS_ENGINE_HEADER`. CI compares both outputs against the pinned 2.2.0 header
at c2c014014046a18f6d318c10e26cc45f4706d06b. It covers three host rates, all
clock/filter modes, eight voices, interpolation reduction, retriggers, release
and capture. This is a regression trace, not a complete equivalence proof.

To render the procedural window auditions:

```sh
g++ -std=c++17 -O2 -pthread tests/window_demo.cpp -o tests/build/window_demo
tests/build/window_demo /absolute/output/directory
```
