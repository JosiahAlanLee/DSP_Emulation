# Development and validation

The [getting-started guide](GETTING_STARTED.md) and individual manuals describe operation. This document covers source builds, tests and implementation scope.

## Source layout

| Directory | Contents |
|---|---|
| pd/vastish | Original vanilla Pure Data Vastish project. |
| vcv/vastish | Native C++ Rack synthesizer and portable engine. |
| vcv/otislab | Rack sampler, playback engine, file handling and loop-rework worker. |
| vcv/spoonbender | Rack effects module, portable engine and capture buffers. |
| vcv/seance | Rack spectral core and four expanders: portable STFT engine and operations, chain link, modules. |
| vcv/shared | Sample-file utilities, versioned sample-sharing interface and panel-glyph sources. |

Build from a complete checkout: plugins include headers from vcv/shared outside their own directories. Generated panels are committed. Regenerating their marks additionally uses the source project described in [the glyph README](../vcv/shared/glyphs/README.md).

## Build a plugin

Download the appropriate [Rack SDK](https://vcvrack.com/downloads/) separately. Existing CI uses SDK 2.6.4. The source requires a C++17 compiler and the SDK build tools, including GNU Make, jq, zip and zstd.

From the repository root, substituting otislab, spoonbender or seance for vastish as needed:

~~~sh
make -C vcv/vastish RACK_DIR=/absolute/path/to/Rack-SDK -j2
make -C vcv/vastish dist RACK_DIR=/absolute/path/to/Rack-SDK
~~~

Use an absolute SDK path. Plugin Makefile defaults differ, so an explicit RACK_DIR is the portable approach.

The .vcvplugin package is created in the plugin's dist/ directory. Each Makefile includes docs/ in its distributables, including the local user manual.

Workflows build Linux x64, Windows x64, macOS arm64 and macOS x64. Live Linux use has been reported, and the packaged Linux plugins load and run headless in Rack Pro 2.6.6 (see [Workstation run](#workstation-run-2026-10-25)); live Windows/macOS use is unverified. Portable engine tests do not establish GUI, audio-driver or complete host behavior.

## Releases

Public downloads are GitHub pre-releases built by [release.yml](../.github/workflows/release.yml). It builds all three plugins for Linux x64, Windows x64, macOS arm64 and macOS x64 (cross-compiled on the arm64 runner), and [tools/bundle.sh](../tools/bundle.sh) packs each platform into one zip with the three `.vcvplugin` files and an INSTALL.txt:

| Platform | Bundle |
|---|---|
| Windows x64 | `DSP_Emulation-<tag>-Windows-x64.zip` |
| Linux x64 | `DSP_Emulation-<tag>-Linux-x64.zip` |
| macOS Apple Silicon | `DSP_Emulation-<tag>-macOS-AppleSilicon.zip` |
| macOS Intel | `DSP_Emulation-<tag>-macOS-Intel.zip` |

To publish:

1. Bump `version` in each changed plugin's plugin.json. Rack 2 only loads versions starting with `2.`, and CI rejects anything else.
2. Update the release name and file names in the install table of [GETTING_STARTED.md](GETTING_STARTED.md). The release job fails if that table doesn't name every plugin's current version.
3. Commit and push, then either push a tag (`git tag preview-2 && git push origin preview-2`, or `vX.Y.Z`) or run **Actions → Release bundles → Run workflow** with the release name filled in. A manual run creates the tag on the commit it built.

Either way the run creates the GitHub pre-release with the four zips. A manual run with the name left empty builds the same zips as workflow artifacts without publishing, as a dry run.

## Tests

Commands below run from the repository root unless otherwise noted. SDK-dependent tests need libRack and its system runtime dependencies available.

### OTIS

~~~sh
make -C vcv/otislab/tests test
make -C vcv/otislab/tests sanitize
make -C vcv/otislab/tests window-sanitize
make -C vcv/otislab/tests thread-sanitize
~~~

See [test documentation](../vcv/otislab/tests/README.md) and [validation](../vcv/otislab/docs/VALIDATION.md) for the Rack harnesses, reference arithmetic and spectral checks.

### Vastish

~~~sh
make -C vcv/vastish/tests all RACK_DIR=/absolute/path/to/Rack-SDK
make -C vcv/vastish/tests check
vcv/vastish/tests/sample_test
vcv/vastish/tests/adapter_test
~~~

Run the dynamic plugin test from the plugin directory after building the plugin:

~~~sh
cd vcv/vastish
./tests/plugin_test
~~~

For primitive comparisons, use a separate Pure Data 0.55-2 checkout, commit 68a30a3e2672d0d57baff7d862d6b611c79d53ec. From the repository root:

~~~sh
make -C vcv/vastish/tests reference_compare PD_DIR=/absolute/path/to/pure-data
vcv/vastish/tests/reference_compare
~~~

See [validation](../vcv/vastish/docs/VALIDATION.md). Full patch identity with Pd is not promised: Rack events use sample timing, Pd uses block scheduling, and noise seeds differ.

### Spoon Bender

~~~sh
make -C vcv/spoonbender/tests engine_test module_test RACK_DIR=/absolute/path/to/Rack-SDK
vcv/spoonbender/tests/engine_test
vcv/spoonbender/tests/module_test
make -C vcv/spoonbender/tests capture_race_test
vcv/spoonbender/tests/capture_race_test
~~~

The last target uses ThreadSanitizer. Linux CI also runs AddressSanitizer/UndefinedBehaviorSanitizer builds and the compiled Spoon Bender-to-OTIS link test. See [the workflow](../.github/workflows/spoonbender.yml) for complete commands.

### Séance

~~~sh
make -C vcv/seance/tests engine_test
vcv/seance/tests/engine_test
make -C vcv/seance/tests module_test RACK_DIR=/absolute/path/to/Rack-SDK
vcv/seance/tests/module_test
~~~

The portable test checks the FFT against a direct DFT, reconstruction at every frame size, latency, the hand-off protocol under random module order and with a stalled chain, RESET continuity and the four operations. The module test wires a core and four expanders as the engine wires them and steps them in random order. Linux CI also runs both under AddressSanitizer/UndefinedBehaviorSanitizer; see [the workflow](../.github/workflows/seance.yml).

### Workstation run, 2026-10-25

Commit `59459b4` on the render workstation: Ubuntu 22.04.5, GCC 11.4, glibc 2.35, Rack SDK 2.6.6 (CI pins 2.6.4).

- Every Linux CI step for the four plugins passed, 41 of 41. That covers the builds, portable and module tests, sanitizers, the OTIS ES5505 reference and 2.2.0 trace comparisons, the Vastish Pd reference comparison, the OTIS–Spoon Bender and Vastish–OTIS links, and the dist packages. ThreadSanitizer ran under `setarch -R` (no address randomization) instead of lowering `vm.mmap_rnd_bits`.
- The four `.vcvplugin` packages loaded in VCV Rack Pro 2.6.6 for Linux, run headless (`-h`) with an empty user folder. A patch with one of each module (OTIS, Séance and its four expanders, Spoon Bender, Vastish) loaded, and the engine ran for 15 seconds with no errors or warnings in Rack's log.
- Not covered: the Rack window and mouse interaction, an audio device, real-time callback load, and hardware (Digi003/ADAT, Expert Sleepers CV). The remaining live checks in each validation document still stand.

## Implementation scope

### OTIS

The engine adapts ES5505 integer interpolation, frequency/address scaling, chip-filter arithmetic and the logarithmic volume table from MAME. Validation compares the documented engine against its references.

Recording, control routing, LAB behavior, windows and REWORK are host-side or original extensions. REWORK uses randomized circular grains, overlap/add and seam/boundary smoothing with limited RMS compensation. It is not Ensoniq's Synthesized Loop algorithm.

Exports and shared samples contain stored mono 16-bit PCM, rather than a render of current playback settings.

### Vastish

The Rack engine ports the Pd Vastish routing, parameters and starting programs. Several primitives derive from Pure Data. LP2RES retains the Pd imaginary filter outlet; HIPASS subtracts a one-pole low-pass; LP4 uses the bob~-derived two-substep RK4 ladder solver.

Sample playback uses four-point Hermite interpolation. The comb's sample-wise feedback preserves a minimum delay associated with the Pd feedback-block implementation; changing sample rate clears comb memory. The output has a 5 Hz DC blocker and no limiter.

Pitch/gate channel counts establish polyphony. FUN cycles resolve the recursive edge as zero. To regenerate programs, run tools/import_programs.py from the plugin directory in a complete checkout. Panel generation uses tools/make_panel.py with fontTools and system DejaVu Sans. Its generated preview is a layout illustration, not a live Rack screenshot.

### Spoon Bender

Both engines use the same original effects topology. ESP applies quantization at selected nodes and delay writes; filters, the shifter and converters use floating point between those points. This is not instruction-level chip emulation or an entirely integer signal path.

Arithmetic helpers are tested against excerpts from a pinned MAME revision. This establishes helper parity, not original Ensoniq effects identity. Historically motivated rates are inferred, and the echo budget is a creative restriction rather than verified EPS memory capacity.

See [SPEC.md](../vcv/spoonbender/SPEC.md) for evidence, freeze treatment and measurements.

### Séance

Original work, not an emulation: the early machines in this repository's remit (EPS/ASR, K2000, DSP4000) were time-domain designs. Analysis and resynthesis use periodic Hann windows on both sides at a quarter-frame hop; the output is divided by the window products actually summed at each sample, so an unmodified frame reconstructs exactly and a restarted grid or a late frame does not break the output. The frame is carried in polar form. Freeze uses the phase-vocoder frequency estimate from the two frames before the hold; Scramble relocates peaks with their lobes and phase-locks them (Laroche and Dolson), which is what keeps a moved partial at its level. Every random decision is a splitmix64 hash of (seed, frame, bin), so replay is exact.

The hand-off is a single token (`FrameV1::stage`) passed with acquire/release atomics; it is correct under Rack's worker threads whatever order the modules are stepped in, and it adds at most one sample per expander, well inside the 64-sample slack that sets the latency.

## Sample sharing

The [versioned interface](../vcv/shared/SampleShare.hpp) exposes completed PCM, rate, name and revision. Receivers copy on the UI thread and check provider identity and interface version. Physical adjacency is required; audio cables are independent.

- Spoon Bender collects a frozen capture into a shareable slot.
- OTIS imports it, retains its playback settings and exposes its finished sample on the right.
- Vastish adopts a copy for SAMPLE. Its handoff keeps sample allocation and disposal off the audio path.

Séance uses the same fixed-pointer idiom for its frame (`vcv/seance/src/Chain.hpp`), but within one plugin binary and on the audio thread, with the frame passed by a token rather than copied. A Séance row and the sample-sharing chain can share a rack but not a position: a Séance module between Spoon Bender and OTIS breaks their adjacency.

OTIS and Vastish embed audio in patches. Spoon Bender saves settings only. Capture history, collected capture slots and live effects tails are not serialized.

## Documentation maintenance

When changing a control, check its visible label, typed-entry unit, CV mapping, clamping, polyphony and saved state. Qualify historical comparisons by what was measured.

Manual patch ideas are suggested starting points, not supplied presets or listening-test results. Identify source material, settings and sample rate for audio examples, and state whether they came from software or physical hardware.

Each plugin.json defines its name, version and slugs. Display names can change; identifiers need compatibility planning once patches depend on them. ESPish identifiers were replaced during pre-release development.
