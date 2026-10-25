# a3-engine — the A³ audio engine

**Status 2026-10-17.** Plan 2 and Plan 3 are the code that is built here. Plan 2: the channel strip
(`src/a3-engine/`: TRIM, 3-band EQ, VOLUME, isolator, level-identical to REAPER's chain, tested in
`tests/engine/`) and `a3-engine-bench` (`bench/`), a headless JACK client with four strips. The Plan 1
code described below (`src/a3-audio-engine/`, `tests/unit/`, `host/`) stays **parked and unbuilt**.
No A³ device runs this engine yet; REAPER remains the audio engine of A³ Core.

## Plan 3: the spatial half

Built: IEM's MultiEncoder and SimpleDecoder/BinauralDecoder compiled from the submodule as nodes
(`src/iem-adapter/`, `src/iem-nodes/`), the `SpatialBus` (order 3, SN3D, 16 channels: encoder, then
the rec, main and booth decoders), and the bench's spatial ports next to Plan 2's (12 with the
default data, 14 with the REAPER 6.1 booth variant; the count follows the files): `rec_L`, `rec_R`,
`<zone>_sub` (only when the zone has a sub) and `<zone>_top_1…N` for main and booth. Channel
positions arrive on the `engine.osc` listener (port 9004) and are clamped like Core. The bench
registers ports and never connects them.

**Timing target** (four strips plus the bus, worst block on the rig): the rig runs PipeWire at
quantum 128 (a3-core 03.0+510), so the acceptance is worst < 0.6 ms per 128-frame block. That is the
same share of the cycle as the stretch goal, worst < 0.3 ms per 64-frame block. `SpatialTiming` only
guards against regressions; the bench's run on the rig decides.

**Spatial data** (`data/spatial/`, installed to `/usr/share/a3-engine/spatial/`): per zone the speaker
layout, the decoder and its parameters, plus the encoder inputs. The default is 4.1 for main and
booth. `data/spatial-reaper-booth-6.1/` (installed beside it) is REAPER's 6.1 booth, for the parity
run: `--spatial /usr/share/a3-engine/spatial-reaper-booth-6.1`. The bench takes `--spatial DIR`, else
`~/.config/a3/engine/spatial/` if it exists, else `/usr/share/a3-engine/spatial/`; the package never
writes `~/.config`. To change the speakers of a zone, copy `/usr/share/a3-engine/spatial/` (or the 6.1
variant) to `~/.config/a3/engine/spatial/`, edit the decoder JSON and parameters, and restart the
bench; the ports follow the files.

**Initialise the submodule** (non-recursive; its nested submodules are never used):
`git submodule update --init third_party/IEMPluginSuite`. In a worktree without network, take it from
the main checkout:
`git -C <worktree> -c protocol.file.allow=always -c url./home/aaa/a3-system/a3-engine/third_party/IEMPluginSuite.insteadOf=https://git.iem.at/audioplugins/IEMPluginSuite.git submodule update --init third_party/IEMPluginSuite`.

**Null tests** (opt-in, they need IEM's installed 1.15 VST3s): configure with
`-DA3_ENGINE_IEM_NULLTESTS=ON` in `build-nulltests/`. Result of 2026-10-17: MultiEncoder
bit-identical, SimpleDecoder at most 9.6e-6 (about −92 dB re RMS) against the installed 1.15 VST3s,
BinauralDecoder about −134 dB.

**Package.** `packaging/stage` installs the bench, both data directories and a copyright file that
carries the MIT, CC BY 3.0 and Apache-2.0 texts (the GPL texts are Debian's
`/usr/share/common-licenses`). Library dependencies come from `dpkg-shlibdeps`; the bench links only
libjack, freetype and fontconfig. IEM's editors are compiled in but never opened, so the X11
libraries JUCE would `dlopen` for a window are not needed.

## Build and test

Prerequisites: CMake 3.16 or newer, a C++17 compiler, JUCE 9.0.3 installed as a CMake package
(`JUCE_PREFIX_PATH`, default `~/local/juce`), `libgtest-dev`, and the JACK development files
(`libjack-jackd2-dev`, found through pkg-config). Without JACK, configure with
`-DA3_ENGINE_BENCH=OFF` to build only the strip and its tests.

`./test.sh` builds (at `nice -n 19`) and runs the tests, which compare the rebuilds against the
unchanged upstream Airwindows plugins in `tests/engine/reference/` (MIT, test-only). Packaging tests:
`python3 -m unittest discover -s tests/packaging` (set `A3_SCRATCH` to a roomy directory if `/tmp`
is small).

- `tests/engine/data/curves-golden.json` is a copy from a3-core (commit 50e7997).
- `tests/engine/data/a3-osc.json` is a copy of the OSC truth from a3-core's branch
  `feat/engine-osc-endpoint` (commit 4014d27: the `engine.osc` listener, port 9004). That branch moves
  to a3-core's `main` on merge; this reference then changes with it.
- `a3-engine-bench` never connects anything. It reads the truth at runtime from `--truth FILE`
  (default `/usr/share/a3/a3-osc.json`).

## Third-party code and data

- **IEM Plug-in Suite** (Institute of Electronic Music and Acoustics, Graz), GPL-3.0-or-later,
  v1.16.0, a git submodule at `third_party/IEMPluginSuite`, compiled from source, unchanged (no file
  in it is edited; its own nested submodules are not used). Initialise it with
  `git submodule update --init third_party/IEMPluginSuite` (not `--recursive`); CMake stops with that
  message when it is missing.
- **KU100 HRIR data** in IEM's BinauralDecoder (`BinauralDecoder/Source/IRs/*.wav`, `EQ/*.wav`),
  GPL-3.0-or-later AND CC BY 3.0: HRIR data: Benjamin Bernschütz, TH Köln, "A Spherical Far Field HRIR/HRTF Compilation of the Neumann KU 100", Proceedings of the 40th Italian (AIA) Annual Conference on Acoustics and the 39th German Annual Conference on Acoustics (DAGA), Merano 2013; dataset https://zenodo.org/records/3928297, licensed CC BY 3.0 (https://creativecommons.org/licenses/by/3.0/). Binaural IRs rendered from it by IEM (MagLS); headphone EQs by Benjamin Bernschütz.
- **Roboto** fonts (Google, Apache-2.0), embedded with IEM's editors, never shown.
- **JUCE** 9.0.3, AGPLv3, as before.

The package copyright (`/usr/share/doc/a3-engine-bench/copyright`) carries the CC BY 3.0 and Apache-2.0
licence texts and the attribution above.

## Plan 1, parked 2026-10-06

### What this is

The C++/JUCE audio-engine foundation written as "Plan 1" (2026-09-21/22) of the A³ app: the first
pieces of an engine that could one day render what REAPER renders today. It was developed inside
`a3-motion-ui` (`src/a3-audio-engine/`, merged there as `0fd6623`, last touched at `ff56cf3`) behind
the CMake switch `A3_AUDIO_ENGINE_ENABLED`, off in the device build.

| Here | Came from (a3-motion-ui) | What it does |
|---|---|---|
| `src/a3-audio-engine/ControlSurface.{hh,cc}` | `src/a3-audio-engine/` | Hands per-channel directions from a control thread to the audio thread without locks. |
| `src/a3-audio-engine/SpatBackendInternal.{hh,cc}` | `src/a3-audio-engine/` | A Motion `SpatBackend` that calls the `ControlSurface` instead of sending OSC. |
| `src/a3-audio-engine/OutputOrder.{hh,cc}` | `src/a3-audio-engine/` | Maps layout outputs to device channels, so a speaker order can be corrected. |
| `src/a3-audio-engine/SpeakerTest.{hh,cc}` | `src/a3-audio-engine/` | Walks pink noise at −40 dBFS from box to box (passed on the rig 2026-09-22, 4.1, no clicks). |
| `src/a3-audio-engine/ChunkedRender.{hh,cc}` | `src/a3-audio-engine/` | The render loop: speaker test → layout buffer → output order, in fixed chunks. |
| `src/a3-audio-engine/CMakeLists.txt` | `src/a3-audio-engine/` | The static library target as it was; it relies on the top-level CMake of a3-motion-ui. |
| `tests/unit/*.cc` | `src/a3-motion-tests/unit/` | GoogleTest suites for the five units above (27 tests). |
| `host/motion-ui-host.cc` | `src/a3-motion-ui/StandaloneApp.*`, `A3MotionAudioProcessor.*` | **An extract, not a compile unit**: the `#ifdef A3_AUDIO_ENGINE_ENABLED` blocks that owned `AudioDeviceManager`/`AudioProcessorPlayer`, switched on `JUCE_JACK` and read `A3_AUDIO_DEVICE_TYPE` / `A3_AUDIO_OUTPUT_DEVICE` / `A3_SPEAKER_TEST`. |

File headers are kept as they were in a3-motion-ui (GPL-3.0-or-later). The commit history lives in
a3-motion-ui: `db8e8a2` (ControlSurface) … `53efd30` (speaker test at −40 dBFS), merged in `0fd6623`,
review fixes `4ef5d80`.

### Why it is here and why it is parked

- **2026-10-06, decided by the maintainer:** a3-motion-ui is a pure OSC interface and contains no
  audio functions. Core does the audio processing, StemDeck plays. So the engine library, the
  channel strip and a future `a3-engine-bench` left a3-motion-ui; they were parked under `engine/` in
  a3-core for a few hours, then given **this repository** (decided the same night). a3-motion-ui now builds with no audio
  device type compiled in (`JUCE_ALSA=0`, `JUCE_JACK=0`), held by its test `MotionIsPureOsc`.
- **2026-10-06 (night):** feature builds stopped; the system is stabilised on today's feature set
  with REAPER. Plan 2 (the channel strip) and Plan 3 (IEM) wait. Hence parked: kept, not built.

The plans and their reasoning (workspace notes, not part of this repository):

- `.claude/notes/2026-09-21-a3-app-und-plattform.md` — the A³ app design, Plan 1, "Wer das Audio
  hostet", the handover list from Plan 1 to Plan 2, the twelve named outputs.
- `.claude/notes/2026-10-06-a3-app-plan-2-channel-strip.md` — Plan 2 (channel strip,
  level-identical to REAPER), "Decided 2026-10-06" and "Parked 2026-10-06".

### What it would need to build

Nothing of this is wired up, on purpose. Whoever takes it up needs:

1. **CMake (3.16 or newer while the precompiled header is kept) and JUCE 9.0.3** (the one JUCE every A³ product builds against; installed as a
   CMake package, `find_package(JUCE CONFIG REQUIRED)`), plus GoogleTest (`libgtest-dev`) for the
   tests. The units use `juce_audio_basics` only.
2. **To be wired into the top-level `CMakeLists.txt`**, which today builds only `src/a3-engine/`,
   `bench/` and the tests: `src/a3-audio-engine/CMakeLists.txt` calls
   `a3_precompile_juce_header()` (defined in a3-motion-ui's top-level CMake) and links
   `a3-motion-engine`. Either drop the precompiled header or bring the helper along.
3. **The Motion coupling decided anew.** `SpatBackendInternal` implements
   `a3-motion-engine/backends/SpatBackend.hh` from a3-motion-ui, and its test drives a
   `MotionEngine` through the constructor that takes a backend. With Motion as a pure OSC device,
   Core would rather feed the `ControlSurface` from the OSC it already receives; then
   `SpatBackendInternal` and its test go, and the engine needs nothing from a3-motion-ui.
4. **A host.** `host/motion-ui-host.cc` shows how the app opened a device; Plan 2 decided against
   JUCE's JACK device (it auto-connects and registers ghost ports) in favour of registering twelve
   named JACK ports itself.
5. **The open handover items** from Plan 1 (logger ownership, thread-safe `OutputOrder`,
   non-throwing `deviceChannelFor()`, >31-channel buffers, `ScopedNoDenormals`, an end-to-end test
   replacing `TheEngineCanSendThroughIt`) — listed in the first note above.

Until then the Plan 1 code is not built by the top-level CMake and not part of the package.
