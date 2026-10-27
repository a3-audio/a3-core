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

## Plan 4a: the graph

`a3::Engine` (`src/a3-engine/Engine.h`) is the whole A³ signal path in one object, tested offline in
`tests/engine/Engine*.cpp`; nothing hosts it yet (the app is Plan 4c). For N channels and D deck slots
(`EngineConfig`, any N ≥ 1):

- **Channels.** Each channel selects its source: its analog pair (the stereo strip of Plan 2 plus the
  DJ filter, into its own moving encoder source and the shared bed) or deck slot k (StemDeck's 16
  B-format channels through the 16-channel deck strip: TRIM, EQ, filter, VOLUME). Analog ↔ deck
  crossfades over 10 ms, deck ↔ deck dips out and in. A path at gain 0 is not processed and is reset
  before it fades in (R14), the analog strip included. A channel without an analog pair is deck-only
  and has no encoder source (R10). Positions, 3D, FREQ and Q do nothing on a deck channel.
- **Buses.** `enc_main`, `enc_fx` (AUX, into IEM's DualDelay) and `enc_phones` (cue pre-VOLUME, mix
  post-VOLUME), order 3. A deck's 16 post-VOLUME channels are added to the buses directly, AUX included
  (R11); its stereo `aux_L/R` is a return source only. The return is the analog return pair or deck k's
  aux pair (R12: decks are 1…D on the wire).
- **Stem cue.** Every deck slot's `cue_L/R` is added after the phones decoder, weighted by the CUE↔MIX
  knob only, whether the deck is on a channel or not (R13).
- **Outputs.** main and booth: SimpleDecoder → MASTER/BOOTH → trim and gate; rec: BinauralDecoder ×
  `trims.rec`, never gated; phones: BinauralDecoder + stem cue → PHONES → trim and gate. The
  `fixedGainDb` stages of `data/spatial/*.zone.json` are not applied: they are the MASTER/BOOTH knobs
  (R1). Every gain control at 0 is exact silence, every send at 0 too (R2, R18).
- **Gate.** A fresh or re-prepared `Engine` has main, booth and phones closed (exactly 0). `gateOpen`
  ramps each to its trim over 1 s, linear in gain; closing reaches 0 within 10 ms. A zone without a
  trim in the config stays closed. `gateLevels()` / `startGateAt()` hand the level over to a rebuilt
  engine.
- **Meters.** `outputMeters()` gives peak and RMS (beat-analyzer's ballistics) per output port, in port
  order: main, booth, rec L/R, phones L/R, then per channel pre L/R and post L/R (a deck channel's
  W on both sides).
- **The /vu meters (O13, since Task 24 of "A³ Core as one program").** The engine measures the 56 meters
  beat-analyzer measured, at the points the patchbay fed it (`tests/engine/data/vu-points.json`):
  `analog1_L`…`analog4_R` = analog inputs 1-8 and `aux_L/R` = the return's analog pair (11/12), raw;
  `main_*`, `booth_*` (tops beyond the layout stay 0), `phones_L/R`, `rec_L/R` = those outputs as they
  leave; `in{n}_pre/post_L/R` = the channel taps above. `VuTaps` (`src/a3-engine/VuTaps.h`) is
  beat-analyzer's `VuMeter` per point (per block: raw peak kept until taken, RMS followed 0.8 / 0.2),
  fed in `process` by `Engine::attachVuTaps`, separate from `outputMeters()` (two readers, two
  accumulators). The host sends them on the control tick at 25 Hz as beat-analyzer's six bundles
  (`VuBundles.h`: `/vu/1-40` in blocks of 10, `/vu/51-66` in blocks of 8, `ff` peak and rms, byte-equal
  to its `serializeBundle`) to Core's `core.vu-relay`, which forwards them; free numbers (9-10, 37-40) are
  sent as 0, 41-50 are StemDeck's. The default map no longer links anything to `beat-analyzer:vu_*`.

**Timing guard.** `EngineTiming.FullGraphUnderOneMillisecondPer128` fails when the best of five
repetition means exceeds 1.0 ms per 128-frame block on the build machine (a3coreV01, i5-4440). It
measures the worst case: all four channels on decks, two stem cues, every send open. Background
load only adds time, so it cannot fail the guard, but a real regression still does. 2026-10-26:
0.84 ms (decks), 0.80 ms (analog). The two BinauralDecoders are about half of it.
`ReportsCostPerChannelAtThirtyTwo` prints the cost of one more channel (about 43 µs analog, 44 µs
deck). The rig's own run decides.

**Known upstream allocations.** IEM's SimpleDecoder copies a `juce::Array` on every call (one small
malloc and free per zone per block), and DualDelay resizes scratch buffers when the block size
changes. Both are upstream and unedited; `EngineAllocation` checks that the engine adds nothing to them.
IEM's DualDelay also sounds slightly different at different block sizes, so the echoes follow the JACK
quantum, as they do in REAPER.

## Plan 4c: the app

**Superseded in part (2026-10-27):** the binary is now `a3-core`, and this engine is its
`--role engine`, without a window (see "The program" below). The text here is Plan 4c as built.

`a3-engine` (CMake option `A3_ENGINE_APP`, default ON, needs libjack) hosts the engine: it reads
the truth (`/usr/share/a3/a3-osc.json`), `engine.json` and the spatial data, runs the signal path
on JACK (the rig) or on a JUCE audio device (anywhere else), listens for Core on `engine.osc`,
says hello on `core.osc` and shows a window.

    a3-engine [--config FILE] [--spatial DIR] [--truth FILE] [--no-connect] [--no-window]
              [--service] [--timing] [--check-config]

- `--config`: else `~/.config/a3/engine/engine.json`, else `/usr/share/a3-engine/engine.json`.
  Save writes to the file the app started with. A saved file is checked the way a start checks it
  and is refused, leaving the old file in place, if the start would refuse it.
- `--no-connect`: the engine makes no JACK link (a run beside REAPER). `--no-window`: no window
  (a unit without a screen). `--service`: closing the window only hides it.
- `--check-config`: prints `ok` or `<file>: <reason>`, exit 0 or 1, opens nothing.

The window has one page per job and a Save bar under them:

| Page | What it edits |
|---|---|
| AUDIO | JACK: rate and quantum, read-only (the server owns them). Device: type, input, output, rate, block size. A choice only edits the config; it opens no device. |
| CHANNELS | N (1..128) with the DSP load beside it |
| ZONES | which spatial directory: the installed `spatial` and `spatial-reaper-booth-6.1`, and `~/.config/a3/engine/spatial`. No layout editor |
| I/O | per engine port its JACK peers (editable) and the live ones the map does not name (foreign: shown, never edited), or its device channel; the direct links the engine makes between two foreign ports |
| CALIBRATION | trim per zone, -90..+6 dB; a slider only proposes, a dialog names the old and the new value, a zone without a trim stays closed |
| FILTER | SVF or Ladder, live: acts at once, needs no Save |
| STATUS | what runs: mode, Core's last word, gate, load, xruns, missing peers, meters |

Edits are pending until Save ("not saved" shows while the edit differs from what runs; STATUS
keeps describing what runs). **Save** writes the file atomically, then applies it: the whole
session is re-created (new engine, new boot id; Core replays its state through the hello). That
holds for every change (N, layout, backend, trims, I/O map) and also works while the backend is
stopped. If the new session does not open, the previous setting is opened again and the error
is shown.

## Plan 4d: the package and the unit

The Debian package is now `a3-engine` (it `Replaces`/`Conflicts`/`Provides` `a3-engine-bench`). It
ships `/usr/bin/a3-core` (the program; Task 28, no `/usr/bin/a3-engine`), `/usr/bin/a3-engine-bench` (kept as a tool), both spatial
directories, `/usr/share/a3-engine/engine.json` (the default config) and the user unit
`/usr/lib/systemd/user/a3-engine.service` and `a3-core-window.service`. The postinst enables the unit for every user and reloads
the running user managers; nothing is written under a home and nothing is restarted.

**The unit** (Task 28). `PartOf=a3-main.service`, and it runs `/usr/bin/a3-core --role engine`
after `jack_wait -w`: no window, so no `DISPLAY`, no wait for the screen and no `ExecCondition`.
There is also no `Conflicts=`: a3-main Wants REAPER's unit and this one in one transaction, and a
`Conflicts=` would drop a start job before any condition runs. The engine always starts (there is no
fallback); REAPER and StemDeck's units stay off through a3-core's own drop-ins (`ExecCondition`
`a3-engine-runs --reaper`), and `a3-engine-runs` belongs to a3-core. `Restart=on-failure` with a
start limit of 5 in 60 s: a failed open (a port taken) restarts a few times and then stops visibly.
`LD_PRELOAD` (the JACK realtime-priority library) and `CPUAffinity=1 2 3` stay. The package `Breaks`
the Python Core (a3-core before the cut) and the metering beat-analyzer (before no-vu); the version
numbers are in `packaging/DEBIAN/control` only. It depends on no Python.

**The window unit** (`a3-core-window.service`). `PartOf=a3-main.service`, `After=a3-core.service
a3-engine.service`: the window attaches to the Core and the engine that are up. It waits for the screen (`DISPLAY=:0` and `%h/.local/bin/a3-wait-for-the-screen`, a
helper of a3-core's package, not shipped here), then runs `/usr/bin/a3-core` with no role, which is
the window. It attaches to Core and the engine and spawns a role only when no unit has it enabled or
running (`systemctl --user is-enabled` / `is-active`; `After=` orders the start only, and Core is
`Type=idle`). `Restart=on-failure`, `RestartSec=2`, `TimeoutStopSec=10` (the quit question can hold SIGTERM): a close, also after the quit question, and SIGTERM
exit 0 and stay closed; a crash or a kill signal starts it again. There is no `[Install]` section
(a3-core's `a3-main.service` Wants the unit, so the postinst enables nothing for it) and no
`Conflicts=`, for the reason above.

**The default config** (`data/engine.json`): four channels on analog pairs 1-4, return pair 6, two
decks, trims main/booth -30.88 dB (REAPER's output faders), phones 0, rec 0, SVF filter, Step 0's delay
modulation, the JACK backend, and the rig's connections: what `a3-patchbay.xml` connected for REAPER,
moved to the engine's ports (analog capture, Scarlett playback, StemDeck's rec input and the
analyzer's beat input `bpm_1`), and every deck port to
`StemDeck-A`/`StemDeck-B` (`bformat_k`, `cue_L/R`, `aux_L/R`; they exist once StemDeck's S4 is
installed, until then they are reported as missing, not an error). Since Task 24 the links into the
analyzer's meter ports (`beat-analyzer:vu_*`, and the direct capture links that only fed them) are gone:
the engine measures those meters itself. Left out on purpose: REAPER's inputs
13-24 (StemDeck's stems and AES67's cue have no engine counterpart) and the AES67 sender, whose client
name carries the machine's host name. The engine reads the file from `~/.config/a3/engine/engine.json`
first; the package never writes there.

## The program: one binary, a3-core

Since Task 23 of "A³ Core as one program" the app is **one binary, `build/app/a3-core`** (CMake
target `a3-core-app`), with roles. Plan 4c's `a3-engine` binary is gone; its engine is the `engine`
role, without a window (the window becomes its own role and reaches the engine over the control
API, P2). The package ships it as `/usr/bin/a3-core` (Task 28); there is no `/usr/bin/a3-engine`.

    a3-core                                   the window: attaches to Core and the engine
    a3-core --headless                        the supervisor: Core and the engine as children
    a3-core --role core [--ip HOST] [--port N] [--mixer HOST:PORT] [--motion HOST:PORT]
                        [--subscriber NAME=HOST:PORT]... [--print-osc] [--web-bind HOST:PORT] [--no-web]
    a3-core --role engine [--config FILE] [--spatial DIR] [--truth FILE] [--no-connect] [--timing]
                          [--check-config]
    a3-core --role screencast-fanout --port N [--linger SECONDS] -- COMMAND...
    a3-core --role i3-bar                     (not built yet)
    a3-core --render-truth user|network-file|network      (not built yet)
    a3-core --truth host NAME | --truth port PROGRAM ROLE (not built yet)
    a3-core --links                           the JACK/PipeWire connections, explained

- Each role takes only its own flags; another role's flag, an unknown one or a missing value is
  exit 2 with the usage.
- **core**: the C++ Core (`src/a3-core`, `CoreService`). Every default is B1's: `--ip` the truth's
  `any`, `--port` `core.osc`, `--mixer`/`--motion` their `osc` listeners, `--web-bind` `core.web`.
  The truth is `$A3_OSC_TRUTH`, else `/usr/share/a3/a3-osc.json`; the network file `$A3_NETWORK`,
  else `~/.config/a3/network.json` (never with an explicit truth); the state under
  `$XDG_STATE_HOME/a3-core`, else `~/.local/state/a3-core`. SIGTERM/SIGINT write state, evening and
  the address list and exit 0; a serve loop that ends by itself exits 1 (the unit decides whether to restart). A
  start that is refused says why on stderr and exits 1.
- **engine**: Plan 4c's flags; `--no-window` and `--service` are still accepted and do nothing.
- **--links**: replaces looking at qjackctl. Prints the audio connections of the running graph, grouped
  by the two programs they join (`Scarlett inputs → engine`, `engine → Scarlett`, `StemDeck A →
  engine`, ...). Ports that run in parallel on both sides are one range (`capture 1–8 → analog 1–8`),
  each with what it is for: a channel, the return, the PA, the booth, the phones, the tempo. The
  meanings are the engine's own port names and config (`analogPairs`, `returnAnalogPair`) and the
  truth's `vu_meters`; level-meter feeds to the analyzer are counted on one line. The last line counts
  the links that `io.jack` of `engine.json` names but whose port is not in the graph, and points to the
  window's STATUS page, which lists them. It only reads: no JACK server is exit 1 with one line on
  stderr; anything else is exit 0. The wording is `app/LinksOverview.cpp` (a pure function, tested in
  `tests/app/LinksOverview.cpp`); reading the graph is `app/LinksCommand.cpp`.
- **screencast-fanout**: `--port` defaults to `$A3_SCREENCAST_PORT`; a port of 0 or no command is
  refused as the Python fan-out refused it (exit 2).
- **--headless**: starts `a3-core --role core` and `a3-core --role engine` and keeps them as the
  engine unit keeps the engine (Restart=on-failure, RestartSec=1, StartLimitBurst=5 in 60 s): a
  failed child starts again after 1 s, a clean end (exit 0, SIGTERM/SIGINT/SIGHUP/SIGPIPE) is left
  alone, a role that fails its fifth start within a minute is given up with one line. Once nothing is
  left to run it exits (1 if a role was given up, else 0); on SIGTERM it ends both (SIGKILL after 5 s). A role whose user unit
  (`a3-core.service`, `a3-engine.service`) is enabled (`systemctl --user is-enabled`) is never
  started a second time.
- **the window** (`a3-core` without a role, Task 26): reaches Core over its HTTP API on the truth's
  `core.web` and the engine over its control API on `engine.control`, both on loopback. It asks both
  once at start; a role that does not answer and whose user unit is not enabled (a laptop) is started
  as the window's own child (`a3-core --role core|engine`, in a process group of its own, so a Ctrl-C
  in the terminal or the terminal closing does not end it), and on the rig, where the units run them,
  nothing is started. Closing asks once, inside the window, "Stop Core and the engine too?" (naming
  only the ones it started); Stop ends only those, Leave running (or Escape) leaves them; a second
  close or a SIGTERM while the question is open is ignored. It never stops a role it did not start
  and never touches a unit; SIGTERM without a question open ends the window and asks nobody. The page list on the
  left carries the engine's pages (AUDIO … STATUS, over the remote view) and, below them, the OSC
  monitor that replaces the Python Core's HTML page: TRAFFIC (direction, address, peer, count, rate,
  last value, age; filter by address, only active = younger than 6 s, one device; the picked row's
  history), TRUTH & DEVICES (Core's fingerprint, each device "is Core's" or "DIFFERS", the audio
  engine), NOT UNDERSTOOD and REGISTER (fetched with FETCH, never streamed), BENCH (to self, mixer
  or motion; the truth's `state.recall`; Core's answer or refusal). The **OSC MONITOR** switch at the
  bottom of the list shows them; it is remembered in `~/.config/a3/core/window.settings`
  (`{"monitor": true}`), written only when it is flipped, off when the file is missing. Only while
  the monitor is on does the window ask Core anything: the traffic from Core's `/api/stream`
  (`/api/traffic` would carry the whole history ring every time) and devices/status/rig once a
  second. The pages copy from the client only when it has news, and TRAFFIC only the rows and the
  history it shows (120 entries), never the ring.
- Everything that asks the operating system (environment, home, host name, unit state, child
  processes) goes through `a3core::Platform` (`src/a3-core/Platform.h`, Linux only).
- The journal: one whole line per write, flushed at once, from any thread.

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
  `feat/plan-4b-engine-mode` (commit 63cdcbe: the engine's words `channel.source`, `aux-return.source`,
  `aux-return.cue`, `delay.bpm`, `engine.open`, `engine.hello`, and what `engine.osc` carries). The
  app's tests read the words the engine understands from `engine.osc`'s `carries` in this copy. That
  branch moves to a3-core's `main` on merge; this reference then changes with it. Refreshed 2026-10-27
  from a3-core `feat/core-truth` 78230b9 (`engine.control`, the meters from the engine: Task 22),
  then from a3-core `feat/core-cut` 62f17a5 (the `vu` meaning names no REAPER outputs);
  `tests/core/EngineTruth.h` loads it for the suites that pin the engine's meters.
- `a3-engine-bench` never connects anything. It reads the truth at runtime from `--truth FILE`
  (default `/usr/share/a3/a3-osc.json`).

## Core in C++

`src/a3-core/` is `a3-core-lib` (namespace `a3core`), a pure library that carries the A³ Core
logic ported from a3-core's Python. It links only `a3-engine-juce`: no audio device, no GUI. Its
own GoogleTest runner is `build/tests/core/a3-core-tests`; `./test.sh --core [gtest args]` builds
and runs just that, plain `./test.sh` builds everything and runs both runners (each prints its
build timestamp). `A3_TARGET=<target> ./build.sh` builds a single target.

The parity guard `tests/parity/test_cases_are_ported.py` checks `tests/core/parity/cases.tsv`
(one row per Python test case: ported, retired or staying in Python) against the `TEST`/`TEST_F`
names in `tests/core` and `tests/engine`. See `tools/parity/README.md`.

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

The package copyright (`/usr/share/doc/a3-engine/copyright`) carries the CC BY 3.0 and Apache-2.0
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
