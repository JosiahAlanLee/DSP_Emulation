# Machinemodule Rack prototype

The pinned Machinemodule desktop core, exposed to **Gorgon per-hit sound locks**
and **VCV Rack polyphonic CV**. This is a separate AGPL-3.0-only plugin;
DrumLab and the other DSP_Emulation plugins keep their own licenses.

The core runs the original voice DSP and ColdFire parameter routines from a
user-supplied Machinedrum OS. Track effects and mixing use Machinemodule's
native implementations. Every track starts from the desktop core's default kit:
level and VOL 100, EQ flat, filter open, pan centred. No Elektron firmware or
sample data is distributed.

## Play it

1. Build/install `MachinemoduleRack`, then add **Machinemodule** in Rack 2.
2. Right-click the module and choose **Load Machinedrum OS (.syx)**. Use the
   supported OS 1.63 image. Loading and emulator initialization happen on a worker,
   which also plays every offered machine once so its DSP code is compiled before
   the first hit (about 0.3 s on a Ryzen 5 5600X). The green light and menu status indicate readiness; the patch saves a file
   path, not a copy of your firmware.
3. For Gorgon: place Machinemodule immediately to the **right of Gorgon 2.4.0**
   or later and leave its **TRIG** input unpatched. The blue light indicates the
   sound link (version 2): Gorgon's lanes play tracks 1–6 for a version-3 score,
   tracks 1–4 for an older one. An older Gorgon can't link to this version. In the matching HydraPy Audio page, open **External Rack instrument**
   and set track defaults or selected-hit locks. Machine IDs and P1–P8 labels
   are available in Machinemodule's context menu after loading the OS.
4. For CV alone: patch a trigger cable, up to 16 channels. Track 1 is channel 1.
   A mono parameter CV broadcasts; shorter poly cables supply zero offsets to
   absent channels. A TRIG cable takes priority over all Gorgon link events.
   For a [byte bus](../../docs/BYTE_BUS_SPEC.md), such as Counter → Lookup Rhythm,
   choose **TRIG reads → Bus** in the menu: on each bus CLK, bits 0–7 that are
   high hit tracks 1–8, held bits included ([bus TRIG mode](docs/BUS_TRIG.md)).
5. Patch **LEFT/RIGHT** to your mixer or audio interface. **TRACKS** carries
   16 individual audio channels after track effects, before pan/level mixing.

A hit resolves the selected machine's native SYN1–8 defaults, then Gorgon
track defaults/cell locks, then the local raw offsets and parameter CV. Values
are rounded/clamped to 0–127. Blank tracker fields inherit. A lock never
carries into the next unlocked hit. Notes remain available on Gorgon's ordinary
V/oct output; this adapter does not guess which machine parameter is pitch.

| Control | Meaning |
| --- | --- |
| MACHINE | Fallback raw ID, 0–191. The loaded OS menu lists valid IDs. |
| MACHINE CV | 0–10 V spans IDs 0–191, rounded; overrides the fallback selector when the hit has no Gorgon machine assignment. |
| P1–P8 knobs | Raw offsets, −127 to +127. Zero preserves the native or locked value. |
| P1–P8 CV | Additive raw offsets: +10 V adds 127, −10 V subtracts 127. Captured at the trigger. |
| VELOCITY | External-trigger mode only: 0–10 V maps to 0–127; unpatched is 127. Zero is a rest. |
| TEMPO | 30–300 BPM for native synced retrigs/LFOs. Match Gorgon's tempo manually in this protocol revision. |
| RESET | Rising edge silences voices before simultaneous hits. Track-filter tails may remain. |
| GAIN | Output scale; 100% gives 5 V per digital full scale. |

CV and knobs latch on each hit. This first adapter does not offer continuous
audio-rate parameter modulation, slides or triggerless locks. Changing controls
under a held gate cannot retrigger it. Hot-plugging into a high gate seeds its
state and waits for the next rising edge. Stop, seek and lost-link transitions
silence voices through the same event queue. Replacing the adjacent Gorgon also
silences the previous voices, discards its buffered packet, and seeds the new
sender's held gates without inventing a hit.

## ROM samples and sampling scope

The OS file supplies synthesis and embedded E12 sounds. UW **ROM machines**
also need the sample directory/data from your own flash image. Machinemodule's
`md-import` / `mdsamples` tools export it as `ROM_SAMPLES.bin` in **MDS1**
format. Choose **Load ROM sample bank** to load that export. An 8 MB flash image,
a WAV file or a random `.bin` is not an MDS1 export. Bad/truncated/overlapping
records are rejected before writing DSP memory. The sample bank is referenced
by path and never saved into a patch or release.

Without the bank, ROM hits are silent and counted as unavailable. With it,
ROM parameters use the same Gorgon/CV hit path as synthesis. RAM, INP, MIDI
and CTR machine groups are not offered and their raw IDs are counted as unavailable. **Live UW recording,
RAM playback, user-WAV mapping and a complete Machinedrum sequencer are not
implemented here.** Master reverb, delay, EQ and dynamics are not enabled in this
first adapter; use Rack effects. Native track effects remain in the core.

## Timing and failure behavior

The Rack audio callback only captures immutable events, moves bounded POD
blocks and reads audio. Firmware loading, JIT work, parameter routines,
resampling and rendering run on the worker. Input jobs contain 128 host samples,
up to 64 complete events. Both SPSC queues hold 16 jobs. Generations invalidate
old jobs after a reload, sample-rate change or input overflow without racing a
queue reset.

Audio has **1024 host samples of queue latency**, plus the converter's reported
causal delay at rates other than 44.1 kHz. Native events round forward to a
32-sample engine boundary, adding 0–31 native samples. At 44.1 kHz, queue latency
is 23.22 ms. The context menu reports total buffering delay and counters. Match
other Rack voices to that delay, or use Gorgon's timing offset to align against
an external reference. All Gorgon lanes share one event/render timeline.

One native track can retrigger once per 32-sample block. Faster same-track
triggers coalesce to the last complete control set and increment a counter.
Input overflow drops the whole job and starts a fresh generation; it cannot
apply half a lock. A missed audio block is silenced and discarded in full,
even if it arrives later. Unsupported machines are counted, with no fallback.
A backend fault stops the engine and reports an error in the menu.

## Build and check

CMake 3.22+, a C++17 compiler, Git, Python 3 and zstd are needed. The dedicated
[platform workflow](../../.github/workflows/machinemodule.yml) builds Linux x64,
Windows x64, macOS Intel and macOS Apple Silicon packages with Rack SDK 2.6.4.
It runs the firmware-free tests on each native architecture. Live firmware audio
and performance measurements below are from Linux only; CI does not have an OS
image or a ROM bank.

```sh
make RACK_DIR=/absolute/path/to/Rack-SDK
make test RACK_DIR=/absolute/path/to/Rack-SDK
# Optional real-core comparison, with your own OS (not a CI fixture):
build/firmware_test /path/to/Elektron_SPS1-1UW_OS1.63.syx
make dist RACK_DIR=/absolute/path/to/Rack-SDK
```

`tools/fetch-core.sh` pins upstream desktop v0.2.1 at
`a2b6cf2478cbe94dfe30bc42da39e221df0e2556`, and initializes only its core
submodules. `CORE_DIR=/path/to/that/checkout` reuses an existing dependency.
For the real Gorgon/Rack interop test, configure CMake with
`-DGORGON_DIR=/path/to/matching/Gorgon` (Rack sound-link implementation).
`-DMD_RACK_SANITIZE=ON` instruments the adapter/control/worker tests with
AddressSanitizer and UndefinedBehaviorSanitizer.

On Windows, use the **MSYS2 MINGW64** shell and its GCC, CMake, Ninja, Python,
Git and zstd packages. MSVC is not compatible with Rack's C++ plugin ABI. The
adapter builds a private MinGW-compatible copy of the pinned emulator in the
build directory; the upstream checkout, voice DSP and firmware are unchanged.
The SDK supplies only Rack's import library: running the real module/load tests
also needs the installed Rack 2 directory containing `libRack.dll` on `PATH`.
The Windows CI extracts the matching official Rack runtime for these tests.

On macOS, build natively for Intel or Apple Silicon. Builds target macOS 11 or
newer unless `CMAKE_OSX_DEPLOYMENT_TARGET` says otherwise. For an explicit target,
pass `CMAKE_ARGS=-DCMAKE_OSX_ARCHITECTURES=x86_64` or
`CMAKE_ARGS=-DCMAKE_OSX_ARCHITECTURES=arm64` to Make and use the matching SDK.
For module tests, set `DYLD_LIBRARY_PATH` to the SDK directory. Packaging fixes
the Rack library install name and applies and verifies an ad-hoc signature.
Package names include `mac-x64` or `mac-arm64`, read from the compiled binary.
If a checkout contains binaries from several targets, select one explicitly
with `python3 tools/package.py --binary /path/to/plugin.dll` (or `.so`/`.dylib`).

Portable tests cover control precedence, restoration, malformed MDS1 input,
100,000 threaded event tuples, rate conversion at 8/22.05/44.1/48/96/192 kHz
and a fractional rate, anti-alias rejection, generations and load recovery.
The real Rack test uses a deterministic backend to check packet flips, both
Gorgon processing orders, CV, trigger precedence and sample-rate changes.
The DSP JIT test executes an original short DSP program on a worker and checks
it against the interpreter, without firmware. The load test opens the compiled
plugin through the OS loader and calls its exported Rack registration entry.
These checks do **not** establish firmware audio fidelity or emulator real-time
performance. The optional `firmware_test` compares the adapter with a separately
driven original core, sample for sample, and checks that the hit is audible.

Measured on 2026-10-26 with OS 1.63 on a Ryzen 5 5600X (Linux, Rack Free 2.6.6,
a private JACK server at 48 kHz):
- `firmware_test` passes. All 50 offered synthesis machines sound; a single hit
  at velocity 127 peaks between −14 and −39 dBFS.
- One core renders 16 busy P-I tracks 6.7 times faster than real time (TRX
  tracks 10.5 times). Before prewarming, a machine's first hit took up to 21 ms
  in one 32-sample block.
- Played live from Gorgon 2.3.0 and HydraPy Studio with 64-, 128- and 256-frame
  periods, with and without ten busy CPU threads, and for ten minutes with the
  tracker editor cueing and editing locks, the menu counters stayed at zero. The
  kick sounds 24.2 ms (±0.35 ms) after Gorgon's gate, with no drift. Each of 489 live
  hits whose lock changes the sound matched an offline render of its expected
  controls rather than the unlocked or the previous hit's sound.

## Source and attribution

- [Machinemodule](https://github.com/sd88me/mpc-vst-machinedrum), desktop
  v0.2.1: AGPL-3.0-only; original voice/host/track effects and firmware decoder.
- Its pinned [dsp56300](https://github.com/sd88me/dsp56300) emulator: GPL-3.0;
  Musashi/68k and asmjit retain the notices in their source dependencies.
- Upstream's [Monomodule](https://github.com/shnolk/monomodule) resampler:
  AGPL-3.0, compiled directly from the pinned checkout.
- `../adapter/`: AGPL-3.0-only, the Rack adapter this plugin shares with the other
  emulated instruments (worker, queues, renderer, sound-link receiver). Its README
  draws the AGPL/BSD boundary.
- `../shared/GorgonSound.hpp`: BSD-3-Clause public message ABI, matching Gorgon's
  `spec/sound-link.hpp`; notice in `docs/GORGON-SOUND-LICENSE.txt`.
- `../shared/ByteBus.hpp`: BSD-3-Clause byte-bus decoder from DSP_Emulation, used
  by bus TRIG mode; notice in `docs/BYTEBUS-LICENSE.txt`.
- Panel labels are DejaVu Sans outlines; notice in `docs/DEJAVU-LICENSE.txt`.

`make dist` creates a `.vcvplugin` and a separate corresponding-source archive
in `dist/`. Distribute them together. The source archive includes the wrapper,
the shared adapter, public ABI, pinned core, required emulator sources and their
license notices.
Firmware and ROM sample data are excluded. To build the source archive without
Git metadata, configure CMake directly: its dependency revision manifest records
the pin. Not affiliated with Elektron, Akai or VCV.

## Changelog

### 2.2.1

- Treat a changed Gorgon instance as a lost link, including reuse of a module
  address with a new Rack ID. Silence the previous voices and seed the new gates;
  the previous sender's still-fresh consumer packet cannot trigger the new link.
- Cover native block edges and simultaneous locked hits with panic in renderer tests.
