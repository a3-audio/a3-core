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

**Package.** `packaging/app/stage` (package `a3-core-app`) installs the bench, both data directories and a copyright file that
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
  post-VOLUME), order 3. A deck's 16 post-VOLUME channels are added to `enc_main` directly and, times the
  channel's AUX send (post-fader, like an analog channel's), to `enc_fx`. Its 16-channel `aux` bus
  (StemDeck's own send) runs through a second deck strip on the channel's controls and joins `enc_fx`
  after the select fade and VOLUME at unity, not times the AUX send: a pulled fader or a deck on no
  channel sends nothing. Decks are 1…D on the wire (R12).
- **FX Return (P45/P47).** The master of the delay channel, like a channel strip whose input is the
  delay's output: RETURN scales it into main, booth and rec (0 is silence); the phones take it as a
  channel, post-RETURN times MIX plus pre-RETURN times CUE (a PFL, `aux-return.cue`). Every analog
  channel reaches the delay through its AUX send, encoded at its position (B-format); there is no analog
  return input and no return source any more. A saved `engine.returnAnalogPair` is a retired key: it
  loads, is ignored and listed (`ignored retired key engine.returnAnalogPair`), and is never written.
- **The delay page (AUX-RTN).** Core's `aux-return.delay.beat/feedback/color/spread` set IEM's
  DualDelay: BEAT with the tempo to delayBPM × delayMult, FEEDBK to the direct and cross feedback (a
  30 ms ramp), both on the audio thread; COLOR to the in-loop filters and SPREAD to ± yaw, glided on the
  control tick. The rules and constants are `src/a3-delay/DelayParameters.h` (plain C++, library
  `a3-delay`, shared with Core). The engine owns the delay (R15): rotate mode, pitch and roll 0, and no
  `delayModulation` of what it sets, whatever a saved spatial directory or `engine.json` says; when that
  overrides a saved value it says so once at start, in `--check-config` and on STATUS. A delay-time
  change above 2 % jumps with an equal-power crossfade (patched DualDelay, see Third-party code), a
  smaller one (the beat clock's drift) keeps IEM's glide.
- **Stem cue.** Every deck slot's 9 `cue` channels (second-order B-format, ACN 0..8) are added to
  `enc_phones` ahead of the phones decoder, weighted by the CUE↔MIX knob only, whether the deck is on
  a channel or not (R13); they share the HRTF and PHONES with the desk's cue.
- **Outputs.** main and booth: SimpleDecoder → MASTER/BOOTH → trim and gate; rec: BinauralDecoder ×
  `trims.rec`, never gated; phones: BinauralDecoder (`enc_fx` + `enc_phones`, which carries the stem cue) → PHONES → trim and gate. The
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
  `analog1_L`…`analog4_R` = analog inputs 1-8, raw; `aux_L/R` = the FX Return after RETURN, its W on both sides;
  `main_*`, `booth_*` (tops beyond the layout stay 0) = those outputs after MASTER/BOOTH and before the
  zone's calibration trim and gate (the desk's master meters; the CALIBRATION page does not move them);
  `phones_L/R`, `rec_L/R` = those outputs as they leave; `in{n}_pre/post_L/R` = the channel taps above; `deck_<letters>` = that deck slot's W as it arrives.
  The truth names `deck_a`…`deck_d` (/vu 37-40), so slots 1-4 have a meter; a slot beyond has none, and the
  engine says so once at start, in `--check-config` and on STATUS. `VuTaps` (`src/a3-engine/VuTaps.h`) is
  beat-analyzer's `VuMeter` per point (per block: raw peak kept until taken, RMS followed 0.8 / 0.2),
  fed in `process` by `Engine::attachVuTaps`, separate from `outputMeters()` (two readers, two
  accumulators). The host sends them on the control tick at 25 Hz as beat-analyzer's six bundles
  (`VuBundles.h`: `/vu/1-40` in blocks of 10, `/vu/51-66` in blocks of 8, `ff` peak and rms, byte-equal
  to its `serializeBundle`) to Core's `core.vu-relay`, which forwards them; free numbers (9-10) are
  sent as 0, 41-50 are StemDeck's. The default map no longer links anything to `beat-analyzer:vu_*`.

**Timing guard.** `EngineTiming.FullGraphAnalogUnderOneDecksUnderOnePointTwoFiveMillisecondsPer128`
fails when the best of five repetition means exceeds its budget per 128-frame block on the build
machine (a3coreV01, i5-4440): 1.25 ms with all four channels on decks, 1.0 ms with all four on
analog (two stem cues, every send open in both). The deck budget is higher because each deck channel
runs a second 16-ch strip for its aux bus (design 4.2); StemDeck A's removed cue decoder pays for it on
the same machine. Background load only adds time, so it cannot fail the guard, but a real regression
still does. 2026-10-26: 0.84 ms (decks), 0.80 ms (analog); 2026-10-27 with the aux strips: about
1.0 ms (decks). The two BinauralDecoders are about half of it.
`ReportsCostPerChannelAtThirtyTwo` prints the cost of one more channel (about 43 µs analog, 44 µs
deck). The rig's own run decides.

**CPU per deck count.** `a3-engine-cpu-bench [--decks N] [--paced]` (`bench/CpuBenchMain.cpp`, no JACK) runs the
whole engine offline at 128 frames and 48 kHz with every deck slot fed on its B-format, AUX and cue
inputs and each deck on its own channel (the others analog, as Core's deck selector allows), and prints mean and p99 block time as a share of the block period
(one RESULT line). Without `--decks` it takes the truth's deck range, as the engine does. It is a
relative tool: the numbers belong to the machine they ran on.

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
| AUDIO | The sound-device settings first: one device-type list, `PipeWire (JACK)` (the JACK backend; `"type": "jack"` in the file) beside JUCE's types. PipeWire: Block and Periods, applied by the engine on APPLY (see "Block and periods on PipeWire"). A JUCE type: input, output, rate, block size, an edit that Save applies; it opens no device. Below them what runs: rate, block x periods, rate source, bit depth, DSP load and xruns, the round trip, measured over the digital loop (MEASURE), and the analog chain (MEASURE ANALOG CHAIN); see "The measured round trip". Each MEASURE is off, with a line naming the I/O page, while its pair is not set |
| CHANNELS | N (1..128) with the DSP load beside it; the deck count (1..26, one letter a slot A..Z; the engine has no cap) with the count that runs |
| ZONES | which spatial directory: the installed `spatial` and `spatial-reaper-booth-6.1`, and `~/.config/a3/engine/spatial`. No layout editor |
| I/O | per engine port its JACK peers (editable) and the live ones the map does not name (foreign: shown, never edited), or its device channel; the direct links the engine makes between two foreign ports; the digital loop and the analog test pair. The digital loop's channels are offered in no route, and a notice names them and what a load unrouted |
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
a3-engine.service`: the window attaches to the Core and the engine that are up. It waits for the screen (`DISPLAY=:0` and `/usr/lib/a3-core/a3-wait-for-the-screen`, a
helper shipped with the program in `a3-core-app`), then runs `/usr/bin/a3-core` with no role, which is
the window. It attaches to Core and the engine and spawns a role only when no unit has it enabled or
running (`systemctl --user is-enabled` / `is-active`; `After=` orders the start only, and Core is
`Type=idle`). `Restart=on-failure`, `RestartSec=2`, `TimeoutStopSec=10` (the quit question can hold SIGTERM): a close, also after the quit question, and SIGTERM
exit 0 and stay closed; a crash or a kill signal starts it again. There is no `[Install]` section
(a3-core's `a3-main.service` Wants the unit, so the postinst enables nothing for it) and no
`Conflicts=`, for the reason above.

**The deck count** (A8, no cap): a config that names `"decks"` keeps it (never raised, never rewritten); one
that names none takes the truth's deck range (`stemdeck.deck-hello`'s upper bound, 4 today), and a Save leaves
it out again. When the engine loads the file from disk at start, each slot whose ports the file links nowhere
links to its local `StemDeck-<X>`, whether or not the file names its count (A9); a slot linked anywhere stays as
saved. A config posted from the I/O page is never filled (A10), so a slot emptied there stays empty until the
next start. Port letters run A…Z, AA, AB, … (`a3::deckLetters`). The count is set on the window's CHANNELS page beside N
(A17, 1..26 there): a count set there is named in the file, a removed slot's links leave the map and an added
slot the map names none of links to its local `StemDeck-<X>`; Save re-creates the engine as for N. A file that
names fewer decks than the truth gets one line at start, in STATUS and from `--check-config` naming the hidden
slots and pointing to that setting.

**The default config** (`data/engine.json`): four channels on analog pairs 1-4 (pairs 5 and 6 free), no deck
count (the truth's), trims main/booth -30.88 dB (REAPER's output faders), phones 0, rec 0, SVF filter, no delay
modulation (R5), the JACK backend, and the rig's connections: what `a3-patchbay.xml` connected for REAPER,
moved to the engine's ports (analog capture, Scarlett playback and the analyzer's beat input `bpm_1`), except
what the round-trip loop needs (2026-10-30): the booth is left unwired (assigned on the I/O page) and analog
11/12 come from device inputs 13/14, because device channels 11-12 are the loop;
deck A to `StemDeck-A` (`bformat_1..16`, `aux_1..16`, `cue_1..9`), deck B to `AES67 deck B` (`receive_MAIN/AUX/CUE…`), and
every other slot to its local `StemDeck-<X>` by the rule above (a StemDeck's ports exist once StemDeck's S4 is
installed; until then they are reported as missing, not an error). Since Task 24 the links into the
analyzer's meter ports (`beat-analyzer:vu_*`, and the direct capture links that only fed them) are gone:
the engine measures those meters itself. Left out on purpose: REAPER's inputs
13-24 (StemDeck's stems and AES67's cue have no engine counterpart) and every link to the AES67 sender: its
client name carries the machine's host name (`AES67 <host name> out`, as `a3-osc-render` names it), so the
file cannot name it for every host. Instead the engine links `rec_L/R` to its own machine's sender,
`AES67 <host name> out:send_CH1/2`, which carries the one recording source to the deck hosts: a built-in link
(P40, P46), named from the same host name `a3-osc-render` uses, added to whatever map is loaded at link time and
never written to the file, so it also holds for an `engine.json` saved before M10. STATUS and `a3-core links`
count it like any other link (missing while the sender is not there). No StemDeck recorder is in the map (P39):
every StemDeck links its own `rec_L/R` from `a3-engine:rec_L/R`; an old saved map that still names
`StemDeck:rec_L/R` makes the same link, which is harmless. The engine reads the file from `~/.config/a3/engine/engine.json`
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
    a3-core --role clock [--device PATH] [--scarlett NODE] [--observe]   the PTP clock steered from the Scarlett
    a3-core --render-truth user|network-file|network      (not built yet)
    a3-core --truth host NAME | --truth port PROGRAM ROLE (not built yet)
    a3-core --links                           the JACK/PipeWire connections, explained

- Each role takes only its own flags; another role's flag, an unknown one or a missing value is
  exit 2 with the usage.
- **core**: the C++ Core (`src/a3-core`, `CoreService`). Every default is B1's: `--ip` the truth's
  `any`, `--port` `core.osc`, `--mixer`/`--motion` their `osc` listeners, `--web-bind` `core.web`.
  The truth is `$A3_OSC_TRUTH`, else `/usr/share/a3/a3-osc.json`; the network file `$A3_NETWORK`,
  else `~/.config/a3/network.json` (never with an explicit truth); the state under
  `$XDG_STATE_HOME/a3-core`, else `~/.local/state/a3-core`. Departments (see "Departments" below) in
  `~/.config/a3/core/departments.json`. SIGTERM/SIGINT write state, evening and
  the address list and exit 0; a serve loop that ends by itself exits 1 (the unit decides whether to restart). A
  start that is refused says why on stderr and exits 1.
- **engine**: Plan 4c's flags; `--no-window` and `--service` are still accepted and do nothing.
- **--links**: replaces looking at qjackctl. Prints the audio connections of the running graph, grouped
  by the two programs they join (`Scarlett inputs → engine`, `engine → Scarlett`, `StemDeck A →
  engine`, ...). Ports that run in parallel on both sides are one range (`capture 1–8 → analog 1–8`),
  each with what it is for: a channel, the PA, the booth, the phones, the tempo. The
  meanings are the engine's own port names and config (`analogPairs`) and the
  truth's `vu_meters`; level-meter feeds to the analyzer are counted on one line. The last line counts
  the links that `io.jack` of `engine.json` names but whose port is not in the graph, and points to the
  window's STATUS page, which lists them. It only reads: no JACK server is exit 1 with one line on
  stderr; anything else is exit 0. The wording is `app/LinksOverview.cpp` (a pure function, tested in
  `tests/app/LinksOverview.cpp`); reading the graph is `app/LinksCommand.cpp`.
- **Departments** (light and video desks told every A³ message, as Mixer and Motion are): the window's
  DEPARTMENTS page (`app/ui/DepartmentsPage.cpp`) edits the list Core keeps in
  `~/.config/a3/core/departments.json` (`src/a3-core/Departments.cpp`), over `GET`/`POST /api/departments`
  (`CoreApi.h`). An entry is checked as it is typed (name non-empty, unique, no space, `@` or `=`, not
  `mixer`/`motion`/`engine`/`self`; host an IPv4 address or a host name; port 1–65535); the POST checks it
  again and also that a host name resolves, so a refused list is never stored. A stored list is written
  atomically and handed to the serve thread: Core subscribes the entries that are on, drops the rest and
  tells one that joins or moved the whole state, all without a restart. At the start a bad file or entry is
  said and left out; it never stops Core. `--subscriber` keeps working: those entries are shown as
  "service file, read-only". **Ruling: a name in both, the command line wins** -- the stored entry is kept,
  reported `overridden`, and Core sends only to the service file's address (a drop-in is the deliberate,
  machine-level choice; the window must not silently re-route it). The report per entry: subscribed, sent
  (messages Core sent that name), `last_sent` (seconds ago, null before the first).
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
  left carries the engine's pages (AUDIO … STATUS, over the remote view), DEPARTMENTS (Core's; asked only
  while shown, not behind the monitor switch) and, below them, the OSC
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

## What each device reports: `device.version`

**Why** (2026-10-30): the Canvas shows, for every row of the rig, the software and firmware it runs and
the machine's two addresses; most rows read "not reported" because no device told Core anything but its
truth hash. This is the contract the devices follow (Motion UI, the desk, the Mixer script, StemDeck,
the beat analyzer). It is **additive**: `device.hello` (`ss`) and `stemdeck.deck-hello` (`s`) are
unchanged, an old sender keeps working and simply reads "not reported".

**What a device sends**, to Core's `core.osc` (the truth's `core` host, port 9000), right after each
`device.hello` -- at start and every 30 s:

    /device/version  s:name  s:version  s:firmware  s:audio

- `name`: the same word as in its `device.hello`: `motion`, `desk`, `mixer`, `prodj`, `stemdeck`, and
  `beat-analyzer` (which sends no hello; the truth lists it as a sender of this word only).
- `version`: its own software version as its package carries it (`03.0+855`, the newest `v*` tag and
  the commits since, as installer/package.py stamps it -- e.g. a3-canvas's `A3_PACKAGE_VERSION` route);
  a build no package made says what it knows (`dev`).
- `firmware`: the version of the firmware on its own board -- Motion: the ESP32 panel's; the Mixer
  script: the Pico's -- as the firmware says it (a tag such as `v03.0+80`, else what it has). `""` for
  a device without a board, or while the board has not answered.
- `audio`: its own IPv4 address on the audio network (AES67/PTP, the truth's `network.audio_address`
  subnet, 192.168.9.0/24 on the rig); `""` for a device on none (the Mixer).
- All four are OSC strings (`ssss`). Core needs `name` and `version`; a shorter message (`ss`, `sss`)
  is taken with the rest as `""`. Its control address is not sent: Core takes the packet's source.
- StemDeck sends it once per instance (from each deck's host); a deck's version is the `stemdeck`
  report from that deck's host.

**What Core does with it**: keeps the newest per name and sending host (a desk or pro-DJ app as
`desk@<host>`/`prodj@<host>`, as its hello is named) and shows it in `GET /api/rig`:

- `devices`: each row as before (`device`, `hash`, `matches`, `age`) plus `host` (the control address of
  its last hello) and that host's `version`, `firmware`, `audio` -- `null` where it has sent no
  `device.version`. `/api/devices` (B1's) is unchanged.
- `versions`: every report, `[{device, host, version, firmware, audio, age}]` sorted by device and host,
  also of a device that says no hello (the analyzer) and of each host a name runs on (StemDeck's decks).

Core's own version is in `GET /api/status` as `version` (the package's, handed in at build time:
`A3_PACKAGE_VERSION` → `packaging/app/stage` → `-DA3_CORE_VERSION`; `dev` for a build no package
made). Core's window API (`core.web`) listens on every interface, so a Canvas on another machine asks
it at the truth's `core` host.

**The machines' addresses** come from the truth's `hosts`, never from code: `<role>` is the machine's
control address (`core`, `motion`, `deckhost-a`…`c`), `<role>-audio` the same machine on the audio
network. A device's own report of `audio` wins over the truth's.

## The one clock: `--role clock`

**Why** (2026-10-30, the maintainer): "core gets clock from scarlett. all devices must rely on it. no
software clock at all. tight latency is mandatory for live setup. ... it should never break." Decks B
and C crackled because the deck hosts' graphs followed PTP, and PTP was the Core's I210 hardware clock
running free, 14.1 ppm away from the Scarlett; the Core's receivers resampled the decks all the time.
Research and measurements: the workspace notes `2026-10-30-one-clock*.md`.

**What runs** (stage 1): `a3-core --role clock` (`a3-clock.service`, a user unit in a3-core-app,
enabled by a3-core's postinst where the Core has an audio socket) reads the Scarlett's sample clock
from `pw-profiler -J` (the driver's clock record: position + delay against CLOCK_MONOTONIC; a client,
not a node, so it cannot cause an xrun) and phase-locks the audio port's PTP clock to it with
`clock_adjtime(ADJ_FREQUENCY)`. ptp4l on the Core stays the grandmaster with `--free_running 1` on the
drop-in's command line (it beats the conffile, which dpkg keeps old if it was ever edited), so it never
touches that clock; the deck hosts follow PTP as before and with it the Scarlett.

**The receivers keep their resampler.** With the clock locked their correction is about 1.0 and costs
nothing; without it, a clock role that is not locked walks the decks' 6 ms FIFO into a resync click within a
few minutes. `resample.disable` on the deck receivers is a later, separate step, only after the clock
role has soaked on the rig (zero rtp-source resyncs, lock held for hours), and only for deck hosts with
a healthy PTP lock (not the Pi). The deck C crackle measured on 2026-10-30 (one corrupted 11-sample
packet about every 13) is a packetisation fault, not the clock.

**Which clocks.** The PHC is the audio port's (the truth's `network.audio_port`, where `ptp4l@<port>`
runs), found under `/sys/class/net/<port>/device/ptp/`; a `--device` that is not that clock, a port
without a PHC or a truth without an audio port is a fault that steers nothing. The Scarlett is its
capture node by name (`--scarlett NODE`, a prefix of node.name, default
`alsa_input.usb-Focusrite_Scarlett_18i20`), learnt from the profiler's driver records; another ALSA
driver is never used instead, it is "no measurement" and the journal names what was seen.

**ptp4l's port state.** On the Core, Debian's sandboxed `ptp4l@` unit (ptp4l 4.4) never answers a
user's `pmc`, whatever the client socket (measured on a3nuc1 2026-10-30; the Pi's unsandboxed 4.2 does).
So a3-core ships `a3-ptp-state.timer` (every 10 s, as root): `/usr/lib/a3-core/a3-ptp-state` (from
a3-core-app) asks `pmc -u -b 0 'GET PORT_DATA_SET'` on ptp4l's own socket and writes
`/run/a3/ptp-state.json` (`portState`, `problem`, `writtenAtUnix`; 0644, a temporary renamed over it).
The postinst enables the timer with AES67 and PTP and starts nothing. The clock role reads the file every
10 s: MASTER and at most 30 s old is the grandmaster confirmed; another state pauses the steering
(HOLDOVER, with the state named in red); missing, stale or "no answer from ptp4l" is not known yet. The
maintainer (2026-10-30): a clock that is synced says LOCKED. The PTP role is a second, neutral line on
the AUDIO page ("PTP grandmaster: confirmed", "PTP role: not read yet (why)"); only a real problem gets
a warning word. ptp4l's sandbox is left as Debian ships it.

**The rules** (`src/a3-clock/ClockSteering.h`, tested against synthetic clocks in
`tests/core/ClockSteering.cpp` and `ClockDaemon.cpp`): only the frequency changes, never the time (no
step, whatever happens). Fast acquisition (2026-10-30, the maintainer: the Scarlett's clock is there at
once, so the PTP clock has to follow at once): the rate is measured for 5 s without writing, the PHC is
set to it in one ADJ_FREQUENCY write, and the phase is anchored. Five seconds tell the rate to about
0.9 ppm rms (12.5 µs per profiler record, 20 a second); a PI with a 20 s time constant pulls the rest in
and goes over to 60 s once the error has stayed inside the lock for a minute. A PHC found within 3 ppm of
the measured rate (a restart on a locked clock) is not written at all. A warm start (the last frequency
locked for at least a minute, kept in `$XDG_STATE_HOME/a3-core/clock.json`, used when the PHC reads 0
after a boot) is written at the first update, checked against the next 5 s, and corrected once if it is
more than 1.5 ppm off. These acquisition writes are the only ones not slew-limited; once tracking, the
frequency moves at most 0.1 ppm/s. Measured in simulation over 200 noise seeds: locked within 10 s cold
and 7 s warm, the anchored error at most about 35 µs (cold) and 55 µs (warm), no lock lost.
The numbers against the decks' 6 ms FIFO (about 3 ms of headroom; 4 ms played clean on the rig,
2 ms crackled): the range is ±40 ppm around the free run,
narrowed to the device's max_adj minus 20 ppm (the I210 allows 62.5); a rate beyond it is a fault
before any write; never locked and |error| above 2 ms is a fault; locked and |error| above 1 ms is a
fault; a phase change above 200 µs plus 1 ppm of the gap (at most 1 ms) is a stepped PHC and is
re-anchored. No measurement for 3 s is holdover (the last frequency held, nothing written); a Scarlett
discontinuity (xrun, restart, replug) re-anchors with the error it had. A refused write or a runaway is
a fault: steering stops, the last frequency is held, and it stays so until `a3-clock.service` is
restarted.

**One instance.** The steering role holds an flock on `<runtime dir>/a3/clock.lock` and on the PHC; a
second one exits 1. Only the holder writes the status file. `--observe` measures and reports in the
journal, never writes the PHC, takes no lock and writes no status. pw-profiler is started again when it
ends or when it brings no Scarlett record for 10 s; a start that fails is said at most once a minute.

**Where it shows**: the role writes `$XDG_RUNTIME_DIR/a3/clock.json` once a second; the engine reads it
(at most once a second) and serves it as `clock` and `clockIndicator` (`word`, `tone`, `detail`: what
Canvas shows) in `/api/engine/status`. The window shows the word in its footer on every page
(`CLOCK LOCKED` in green, caution or danger otherwise) and a CLOCK section on the AUDIO page, which
keeps at least a quarter of the page in every layout: master, state and phase error, device and port,
the frequency, PTP against the Scarlett now, the counters, framed in caution (acquiring, observing) or danger (holdover, fault, not running). The journal gets one line every 10 s
(`a3-core clock: locked error +2.3 us freq +14.102 ppm rate +0.012 ppm corrections … reanchors … steps
… holdovers … losses … breaks … feed-restarts … port MASTER|unknown`) and one at every state change.

**Write access**: `/dev/ptp*` is `root:clock 0664` on Debian; the postinst puts the app user into
`clock` (effective once the user manager starts again: a reboot). Without it the role measures and
reports a fault at its first write.

**Clock master**: the AUDIO page offers one master, the Scarlett steering the PTP grandmaster. An
external PTP grandmaster would make the Core a PTP client and need the Scarlett on word clock; not
offered in this version.

## The measured round trip

**Why** (2026-10-30): the AUDIO page showed an estimate (capture + one period + playback latency as PipeWire
reports them). The rig's Scarlett 18i20 has its S/PDIF out (device outputs 11-12) patched to its S/PDIF in (device
inputs 11-12); the maintainer: "make 11-12 free for the measurement".

**The loop** is `io.roundTripLoop` in `engine.json`: `{"output": 11, "input": 11, "channels": 2}` (device channels,
1-based as the I/O page shows them; the probe uses the first of each side, all `channels` are reserved). A file
without the key gets exactly that default, so the maintainer's existing file is covered; `null` is no loop.
Reserved means: no dropdown offers those channels, the port editor refuses them, and a route onto them in a
loaded file (a JACK peer, a device channel, a direct link) is dropped and listed (`AppConfig::loopDropped`: on
the I/O page's notice, on stderr at start and from `--check-config` as `unrouted <route>: the round-trip loop`).
A JACK peer counts as a device channel when it is `system:playback_N`/`system:capture_N` or PipeWire's pro-audio
`…pro-output-*:playback_AUXn`/`…pro-input-*:capture_AUXn` (AUX0 = channel 1). On JACK the engine registers
`roundtrip_out`/`roundtrip_in` (not engine ports of the map) and links them to `system:playback_<output>` /
`system:capture_<input>` at link time, like the AES67 sender link; on a device it uses those device channels.

**The measurement** (`app/RoundTripProbe.h`): an 8191-sample maximum-length sequence at -20 dBFS (0.19 s at
44.1 kHz) only on the loop output, the loop input recorded for 24575 samples (loops up to 16384 samples), and
the delay is the lag of the cross-correlation's peak (FFT, on the control thread). A peak less than 6 x the
largest correlation away from it, or a recording that is less than half the signal at that lag (coherence
< 0.5), is "no loop found", never a number. The audio thread only copies into buffers made at open and hands
over through one atomic state. It runs 2 s after the backend opens, after a new quantum or rate, 3 s after WirePlumber is back from new periods, on
`POST /api/engine/roundtrip` and on the AUDIO page's MEASURE. The number is the loop's out-to-in time as the
engine sees it: playback + capture latency + one period.

**Choosing the loop** (2026-10-30): the I/O page's `Digital loop` row, one channel per side; `none` is no loop
(`null` in the file). A first channel sets both sides (out N -> in N, as an S/PDIF cable runs); the loop keeps its
channel count. The reservation stays: a channel the loop would take that carries a route or the analog test pair
is refused, naming its user, and never taken from it.

**The AUDIO page** says `Round trip (measured, digital loop)   X ms (N samples), HH:MM`, or the estimate as the
fallback, labelled with why: `Round trip (estimated, no loop configured | no loop found on output 11 -> input 11 |
measuring | not measured yet)   …`. The digital loop leaves out the AD/DA converters.

**The analog chain** (analog in -> AD -> engine -> DA -> analog out; the maintainer: what matters most). An
optional `io.analogLoop` `{"output": 6, "input": 8}` names a pair for a temporary cable, chosen on the I/O page
(not reserved: it is refused while any route uses either side). Only MEASURE ANALOG CHAIN (`POST
/api/engine/roundtrip/analog`) runs it, never anything automatic: the same signal at -20 dBFS on the chosen output
only, and the digital loop in the same run. The converters' share = analog - digital is written at once into
the config file as `converterSamples`, `sampleRate` and `measuredOn` (only that key changes; the user's file,
created from the running one if needed). The page then says `Analog chain   X ms (measured: digital loop now +
converters measured on <date>)`, the digital part being the live loop measurement; without a share it says the
converters are not measured yet. Clearing the pair on the I/O page keeps the share (it belongs to the hardware).

## Block and periods on PipeWire

**Why** (2026-10-30, the maintainer: "I want these settings in the Engine's AUDIO page"): the rig ran a fixed
quantum of 128 from a3-core's PipeWire config and whatever periods PipeWire chose for the Scarlett (3 on the rig:
ALSA period 128, buffer 384; measured digital loop 343 samples, 7.8 ms).

**Stored** in `engine.json`'s backend section: `blockSize` (32..2048; the page offers 32, 64, 128, 256, 512,
1024) and `periods` (2 or 3). Defaults 128 and 3: today's behaviour, nothing changes on install. A change on the
page is written at once (only those two keys, like the converters' share), no Save.

**Block**: `pw-metadata -n settings 0 clock.force-quantum N`, live; the engine follows through its buffer-size
callback and measures the round trip again. 128 writes 0 (no force), so PipeWire's own quantum from a3-core's
`pipewire.conf.d` runs as before. PipeWire 1.6 does not clamp a forced quantum to `clock.min-quantum`
(`settings.check-quantum` is off by default; the graph takes min = max = force), so min-quantum 128 stays untouched
and nothing needs restoring. The engine forces its stored block at every start: it remembers it, nothing else sets
it. The engine requests no `node.latency` of its own (jack.conf's 128 applies to every JACK client); a forced
quantum overrides every client's, so the REAPER/CLAP trap of 2026-10-25 (force above node.latency) is gone with
REAPER, but another JACK client that cannot follow a buffer-size change would show it first.

**Periods**: `api.alsa.period-num` in the engine's own WirePlumber 0.5 drop-in,
`~/.config/wireplumber/wireplumber.conf.d/80-a3-engine-alsa.conf`, matching by exact `node.name` only the ALSA nodes
the engine's I/O map reaches (`system:*` is the default sink and source, read with `pw-metadata -n default 0`; a
peer `alsa_…:…` names its node; the loops are on `system`). Then `systemctl --user restart wireplumber`: the
device's nodes go and come back (about two seconds of silence), the engine links its ports again (port
registration and the once-a-second plan) and measures the round trip 3 s after WirePlumber is back. 3 removes the
file; nothing else in that directory is touched. The file is written atomically (a hidden temporary renamed over
it). Periods are never applied at the engine's start: WirePlumber reads the file at its own start.

**The page**: a choice is applied only on APPLY; the line before it says "applies live" for a block and "the
audio stops for about two seconds while WirePlumber restarts" for periods. REVERT goes back to the previous
choice (a block at once, periods after the same question). The info below then shows the new `Block N frames x
M periods`, DSP load and xruns, and the measured round trip. Commands run through `posix_spawn` (never fork, the
engine is large; `app/CommandRunner.h`) on the control thread. Over the control API: `POST /api/engine/timing`
`{"block", "periods"}`, and the status's `timing` (applied, previous, restarting, message). On a JUCE device type
the block is the device's buffer size (Save) and there are no periods (JUCE's ALSA device fixes its own).

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
  v1.16.0, a git submodule at `third_party/IEMPluginSuite`, compiled from source; no file in it is
  edited (its own nested submodules are not used). One plug-in is patched in the build tree:
  `patches/iem/DualDelay-crossfade.patch` (a delay-time jump crossfades instead of gliding) is applied at
  configure time to a copy of `DualDelay/Source` under the build directory, and configure stops,
  naming the patch, when it no longer applies. Initialise it with
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
