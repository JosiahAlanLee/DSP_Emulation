# OTIS, Vastish, Spoon Bender and Séance

Four plugins for VCV Rack 2. The first three are instruments, usable separately or as a sample-based sound-design system; Séance is a spectral effect built from a core and five expanders.

| Module | Use it for | Manual |
|---|---|---|
| **OTIS Sampler** | Record audio, transpose it through an ES5505-derived playback engine, scan short loops, and reshape stored samples. | [OTIS](../vcv/otislab/docs/MANUAL.md) |
| **Vastish** | Play an eight-voice synthesizer with selectable DSP routing, sample playback, and modulation outputs. | [Vastish](../vcv/vastish/docs/MANUAL.md) |
| **Spoon Bender** | Add stereo echo, frequency-shifted feedback, reverb and frozen tails; capture a phrase after hearing it. | [Spoon Bender](../vcv/spoonbender/docs/MANUAL.md) |
| **Séance** | Hold a sound's spectrum in place, scatter its partials, shape it with a second signal, or sweep a wall across it, with expanders working on one shared spectral frame. | [Séance](../vcv/seance/docs/MANUAL.md) |

OTIS, Vastish and Spoon Bender draw on early digital instrument architectures and add new patching and sample-manipulation features; Séance is original work, not modelled on a historical instrument. They include original programs and a procedural demo sample. Manufacturer firmware, sample ROMs and effects microcode are not required or included.

## Install

1. Open the [releases page](https://github.com/JosiahAlanLee/DSP_Emulation/releases) and download the zip for your computer. No GitHub account is needed.
2. Unzip it. Inside are the `.vcvplugin` files and `INSTALL.txt`. Don't unzip the `.vcvplugin` files themselves; Rack does that.
3. In Rack, choose **Help → Open user folder**, then quit Rack.
4. Copy the `.vcvplugin` files into the plugins folder for your platform, inside that user folder.
5. Start Rack. Right-click the rack and search for **OTIS Sampler**, **Vastish**, **Spoon Bender** or **Séance**.

Files in the current preview release, **preview-1**:

| Platform | Download | Contains | Copy into |
|---|---|---|---|
| Windows x64 | `DSP_Emulation-preview-1-Windows-x64.zip` | `OtisLab-2.5.2-win-x64.vcvplugin`<br>`VastishLab-2.3.1-win-x64.vcvplugin`<br>`SpoonBenderLab-2.1.1-win-x64.vcvplugin` | `plugins-win-x64` |
| Linux x64 | `DSP_Emulation-preview-1-Linux-x64.zip` | `OtisLab-2.5.2-lin-x64.vcvplugin`<br>`VastishLab-2.3.1-lin-x64.vcvplugin`<br>`SpoonBenderLab-2.1.1-lin-x64.vcvplugin` | `plugins-lin-x64` |
| macOS Apple Silicon | `DSP_Emulation-preview-1-macOS-AppleSilicon.zip` | `OtisLab-2.5.2-mac-arm64.vcvplugin`<br>`VastishLab-2.3.1-mac-arm64.vcvplugin`<br>`SpoonBenderLab-2.1.1-mac-arm64.vcvplugin` | `plugins-mac-arm64` |
| macOS Intel | `DSP_Emulation-preview-1-macOS-Intel.zip` | `OtisLab-2.5.2-mac-x64.vcvplugin`<br>`VastishLab-2.3.1-mac-x64.vcvplugin`<br>`SpoonBenderLab-2.1.1-mac-x64.vcvplugin` | `plugins-mac-x64` |

preview-1 predates Séance. The next release adds `SeanceLab-2.0.0-<platform>.vcvplugin` to each zip.

Not sure which Mac you have? Apple menu → **About This Mac**: "Chip: Apple M…" is Apple Silicon; "Processor: Intel…" is Intel.

To update, quit Rack, delete the old `.vcvplugin` files from the plugins folder and copy in the new ones, so only one version of each plugin is there.

Each module works on its own; install OTIS, Vastish and Spoon Bender together to use the sample-sharing links.

OTIS, Vastish and Spoon Bender have been used live in Rack on Linux; Séance has been loaded and run headless there but not yet played live. The Windows and macOS builds compile and pass the portable engine tests in CI, but nobody has run them inside Rack yet. If one doesn't appear in the module browser, Rack's `log.txt` in the user folder says why; please report it in [Issues](https://github.com/JosiahAlanLee/DSP_Emulation/issues). To build from source instead, see the [development guide](DEVELOPMENT.md).

## First sound

Configure Rack's Audio module for your output device and start with its monitoring level low.

### Play OTIS

1. Add **OTIS Sampler** and choose **Load included demo** from its right-click menu.
2. Patch OTIS **L** to an Audio output channel or mixer input.
3. Press **PLAY**. Change **PITCH**, or set **LOOP** to **FWD** for a repeating sample.

OTIS plays a mono sample on both L and R, and a stereo sample (a two-channel WAV, or a take with AUDIO R patched) on L and R separately. A gate or trigger starts playback; the default one-shot behavior lets playback continue after the gate falls.

### Play Vastish

1. Add **Vastish**. Its initial program is **Ladder Bass**.
2. Patch pitch CV into **V/OCT** and the corresponding gate into **GATE**. Core MIDI-CV can supply both from a keyboard.
3. Patch Vastish **L/R** to your mixer or Audio module.
4. Play a note. Click the program name to try the other starting programs.

For chords, set your pitch/gate source to polyphonic operation. Vastish uses up to eight channels.

### Add Spoon Bender

1. Patch your sound source into **IN L**. For stereo audio, connect both **IN L** and **IN R**.
2. Patch Spoon Bender **OUT L/R** to your mixer or Audio module.
3. Set global **MIX** to 0% to hear the input, then raise it to hear the stage mixes. **ECHO MIX** blends echo into the signal feeding the space; **SPACE MIX** blends in reverb.
4. Change **TIME** and **DECAY**. Set **SHIFT** to zero for unshifted repeats.

Leaving IN R unpatched sends IN L to both processing channels. Spoon Bender's CLOCK input accepts timing pulses; OTIS's CLK CV input instead controls its playback clock in volts per octave.

### Try Séance

1. Patch a sound into Séance **A** and Séance **OUT** to your mixer or Audio module.
2. Place a **Séance Freeze** or **Séance Wipe** expander directly to its right, touching it. The expander's **LINK** light comes on.
3. Press **FREEZE** on Freeze to hold the sound's current spectrum, or turn **WIPE** on Wipe to sweep a wall across it.

Expanders act only in an unbroken row to the right of the core, applied left to right. Séance's output is delayed by its frame size plus 64 samples, about 44 ms at the default size and 48 kHz; **MIX** blends in the dry signal, delayed to match. Séance is not part of the sample-sharing chain below.

**Séance Bridge** adds a byte-bus connection: eight incoming bits open eight
frequency bands, while BUS OUT reports activity before that mask. Its
[OTIS feedback example and controls](../vcv/seance/docs/BYTE_BUS.md) need
ByteBusLab 2.2.0, OTIS 2.8.0 and SeanceLab 2.1.0.

## Connect the sample-sharing chain

Place the modules immediately next to one another in the same row, in this left-to-right order:

**Spoon Bender → OTIS → Vastish**

This placement establishes two optional sample-data links. **Audio still travels through patch cables.** You can route Vastish L/R into Spoon Bender while keeping this physical order.

1. In OTIS's menu, enable **Follow Spoon Bender captures**.
2. In Vastish's menu, enable **Follow OTIS on the left**. This selects its SAMPLE wave.
3. Play audio through Spoon Bender and press **CAPTURE** after a phrase you want to keep.
4. OTIS imports the capture. Vastish adopts OTIS's completed sample on a subsequent UI refresh.
5. Trigger playback in OTIS or play a new gate in Vastish.

With follow enabled, a new capture replaces OTIS's current sample, and changes to OTIS's completed sample update Vastish. Export an OTIS WAV or save a separate patch to keep an earlier generation.

### What the links carry

| Link | Transferred | Kept at the destination |
|---|---|---|
| Spoon Bender → OTIS | A completed 16-bit recording of Spoon Bender's post-MIX output, with its recorded sample rate: stereo from Spoon Bender 2.2 into OTIS 2.7, otherwise a mono mix. | OTIS's playback controls. The import replaces sample memory and stops its voices. |
| OTIS → Vastish | OTIS's entire completed 16-bit sample, with its sample rate: stereo from OTIS 2.7 into Vastish 2.4, otherwise a mono mix. | Vastish's routing, envelopes and other synthesis settings. |

Vastish does not inherit OTIS's selected window, loop points, reverse setting, chip-clock setting or playback filters. To include those sounds, record OTIS's audible output into a new take, or process and capture it through Spoon Bender.

The links copy completed samples while the Rack UI is running. They do not stream audio. A recording still in progress is not transferred. Replacing a sample while Vastish is sounding can click.

## A first experiment: turn a reverb tail into an instrument

This is a suggested patch to explore, not a supplied preset.

1. Send a chord or sustained tone through Spoon Bender. Raise SPACE MIX and DECAY until the tail is easy to hear.
2. Engage **FREEZE**, then release the source notes. The reverb holds its existing tail.
3. Let it run for the capture duration you want, then press CAPTURE. Import the result into OTIS.
4. In OTIS, set LOOP to FWD and WINDOW mode to SCAN. Set SIZE to 256 samples and move POSITION slowly.
5. Enable the OTIS follow link in Vastish. Play its SAMPLE wave through a filter or shaper and its envelopes.

OTIS's short-loop scan changes OTIS playback only. Vastish still receives the whole captured tail. To play the scanned result through Vastish, first record that OTIS output as a new sample.

## Save your work

OTIS and Vastish embed their completed sample data in Rack patches. Their controls and follow settings are also saved. Finish recording or reworking before saving the version you intend to retain.

Spoon Bender saves its settings, but not its rolling capture history, captured slot or live echo/reverb tails. Import a capture into OTIS and save the patch to retain that audio. Each capture starts a new history period; a second capture shortly afterward contains only the audio since the first. Changing Rack's sample rate clears Spoon Bender's history and any capture still awaiting collection.

## If something is missing

| Symptom | Check |
|---|---|
| Module does not appear in the browser | Install the right platform build, restart Rack, and use a 2.x plugin version with Rack 2. |
| Vastish is silent | Check GATE, pitch/gate channel counts, AMP settings, MASTER, and whether SAMPLE is selected without a loaded sample. |
| OTIS plays after gate release | Enable **Gate release stops playback (default: one-shot)**, or use an envelope into VCA. |
| Sample does not follow | Check immediate adjacency, direction, follow menu settings and compatible plugin versions. Keep the Rack UI running. |
| Spoon Bender capture is shorter than LENGTH | Its available history may be shorter than the requested duration, especially after another capture or a sample-rate change. |
| OTIS import is marked TRUNCATED | The recording exceeds OTIS's sample capacity. At 96 kHz, its memory holds about 10.9 seconds. |

For a bug report, include the plugin versions, OS/CPU, Rack version, sample rate, relevant settings and a small reproduction patch. Use original or redistributable audio when attaching a sample. Report issues in the [repository tracker](https://github.com/JosiahAlanLee/DSP_Emulation/issues).
