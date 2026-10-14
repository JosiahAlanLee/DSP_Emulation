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

`lab_test` covers the 2.2.0 LAB controls (clock, interpolation bits, resample source, per-voice START/END) and runs as part of `make -C tests test`.
