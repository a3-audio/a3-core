# Core scenarios

A scenario is a short evening scripted as data: the files Core finds on disk, its arguments, and
what arrives when. `tools/parity/record_scenario.py` runs it against B1's Python Core (a3-core
7a7b2441f74e, `a3-core.py` loaded in-process) and writes down everything that Core did — the
*transcript*. `ScenarioRunner` (tests/core/ScenarioRunner.cpp) runs the same scenario against
`a3core::Core` on a fake network and a fake clock and compares. `Scenarios.<CamelCaseName>` in
tests/core/Scenarios.cpp is one test per scenario.

Each scenario is two files: `<name>.json` (written by hand) and `<name>.expected.json` (recorded,
never edited by hand — except where a section below says the Python Core has no such path).

## Recording

    A3_CORE_BASELINE=/home/aaa/a3-system/wt/parity-baseline/a3-core-7a7b2441f74e \
        /home/aaa/.venv/bin/python3 -I tools/parity/record_scenario.py tests/core/scenarios/<name>.json [...]

From the repo root. `A3_CORE_BASELINE` is the exported B1 tree (`git archive` of a3-core at B1);
the venv's python-osc is 1.10.2, the rig's. Several scenarios at once are recorded one process
each. Recording twice gives byte-identical files (`cmp`); if it does not, the recorder has lost its
determinism and that is a bug in `tools/parity/scenario_core.py`, not in the scenario.

## Adding a scenario

1. Write `tests/core/scenarios/<name>.json` (format below; an `"about"` line saying what it shows
   is welcome and ignored by both sides).
2. Record it (above) and read the transcript: it is what B1 does, and the scenario is only worth
   having if that is the behaviour you meant to pin.
3. Add `TEST (Scenarios, <CamelCaseName>) { expectAsPython ("<name>"); }` to
   tests/core/Scenarios.cpp and run
   `flock /home/aaa/a3-system/wt/.build.lock nice -n 19 env JOBS=2 ./test.sh --core --gtest_filter='Scenarios.*'`.
   On a difference the test names the first twelve and where the whole C++ transcript was written
   (`build/tests/core/scenario-<name>.actual.json`, beside the runner).

## The scenario

```json
{
  "files": { "state.json": {…}, "evening.json": {…}, "seen.json": {…} },
  "args":  { "subscriber": ["light=192.168.8.40:9000"], "mixer": "host:port", "motion": "host:port" },
  "steps": [
    { "at": 0.0, "from": ["192.168.8.11", 50123], "osc": ["/channel/1/gain", ["s", "0.5"]] },
    { "at": 0.05, "from": ["127.0.0.1", 40000], "packet": "<base64 datagram>" },
    { "at": 0.1, "tick": true },
    { "at": 3.0, "flush": true },
    { "at": 4.0, "from": ["127.0.0.1", 40001], "vu": "<base64 datagram>" }
  ]
}
```

- **files**: written into `$XDG_STATE_HOME/a3-core/` before Core starts. A JSON value is written as
  `json.dumps(value, indent=2)` + newline; a **string** is written verbatim — that is how a
  half-written or otherwise broken file is given. Only these three names.
- **args**: all optional. `mixer`/`motion` default to the truth's endpoints, `subscriber` to none.
  Core's own port is the truth's `core.osc`.
- **steps**, in order, `at` never going back — both sides refuse a step that does (seconds after
  start; start-up itself is at 0.0):
  - `osc`: one datagram from `from` = `[host, port]`: the address, then each argument as
    `[tag, value]` — `i`, `h`, `f`, `d`, `s`, `T`, `F`, `N` (value `null`), `b` (base64). Encoded
    by python-osc on the recording side and by `OscWire` on the C++ side.
  - `packet`: a raw datagram (base64) from `from` — bundles, damaged packets.
  - `tick`: one serve-loop tick (`Core::tick`, B1's `on_tick`), then the debounced writes that fell
    due: `state.json`, then `evening.json` (due 2 s after the change that made them), then the
    address list's look (every 5 s from start-up; writes when the list changed and has settled).
    **A tick is never implied**: B1's serve loop ticks after every pass, so put a `tick` after a
    message wherever the tick's work (silences, deck presence, LIVE repeat, writes) matters.
  - `flush`: B1's `stop()` (`Core::stop`): `state.json` and `evening.json` written if anything is
    pending, then `seen.json` written always. Normally the last step.
  - `vu`: a datagram on `core.vu-relay` (`Core::handleVu`).

After every step (and after start-up), what Core sent its own port (`to: "self"`) is fed back as
one pass of the serve loop — the newest position per key wins, as `a3_core_latest.serve` — from
the socket Core's replays leave from, `127.0.0.1:50000`, which the hello gate trusts. That repeats
until Core sends itself nothing more. The engine's replay thread in B1 is started and joined at
once, inside its hello handler; the C++ Core sends the replay from the serve thread (P8).

**This interleaving is a modelling choice, not B1's real scheduling.** On the rig the replay thread
and the serve loop run side by side: the replayed values arrive over several passes, with ticks
between them, possibly interleaved with other senders' packets. The model (whole replay first, then
one pass per round of self-sends, no tick in between) is the one deterministic order both sides can
agree on; Task 19's engine-hello scenarios pin that order, not the rig's timing.

**A handler that raises.** B1's serve loop catches it per packet and prints the traceback on stderr
(`a3_core_latest.serve`); the rest of that packet's messages are not handled. Both sides record it
as one entry `{"at", "journal": "handler raised: <ExceptionType>", "stderr": true}` — Python's type
name (`ValueError`, `KeyError`…). A C++ port that must fail where B1 fails throws an exception whose
`what()` is that Python type name; the runner catches per packet as the serve loop does. A
`notPortedYet` stub (below) reached from a handler shows up here with its own text, so it is red
and readable.

## The transcript

A JSON list, in the order things happened:

```json
[
  { "at": 0.0, "to": "mixer", "host": "192.168.8.11", "port": 7772, "address": "/channel/1/3d", "args": [["f", 0.6200000047683716]] },
  { "at": 0.0, "journal": "startup: spoke the remembered state, 22 messages to 2 subscribers", "stderr": false },
  { "at": 3.0, "file": "seen.json", "text": "{\"version\": 1, …}", "json": {…} },
  { "at": 4.0, "to": "vu", "host": "192.168.8.30", "port": 7774, "raw": "<base64>" }
]
```

- **A send**: every datagram one of Core's clients sends, named as in Python: `mixer`, `motion`,
  `engine`, `stemdeck`, `stemdeck-A`…, `desk@<host>`, `prodj@<host>`, the `--subscriber` names, and
  `self` for Core's own port. `args` are read off the encoded datagram: python-osc's tags exactly
  (`int` → `i`, `float` → `f`, `str` → `s`, `bool` → `T`/`F`, a list → several arguments, `None` →
  no argument), a float as the float32 it became. REAPER's, the IEM encoders' and the DualDelay's
  clients are silent in engine mode and never appear.
- **A journal line**: every `print`, `stderr` true for `file=sys.stderr`. The state directory is
  written as `<state>` (so `<state>/seen.json`), since it is a temporary directory on both sides.
- **A file write**: `{"at", "file", "text", "json"}` — `state.json`, `evening.json` or `seen.json`,
  `text` the exact bytes just written (`state.json`/`evening.json`: `json.dumps(…, indent=2)` +
  newline; `seen.json`: `json.dumps(…)`, separators `", "` and `": "`), `json` the same parsed, for a
  readable difference.
- **A meter forward** (`vu` steps): the bytes forwarded unchanged, client name `vu`.

The runner compares entry by entry: same order and count; `at`, `to`, `host`, `port`, `address`
equal; arguments of equal tags, `f` equal as `float`, `d` as `double` (a NaN equal to a NaN: both
sides write it as `NaN`), `i`/`h` as integers, `s`/`b` as text; journal lines and their stream equal; files byte-equal (`text`).

## The clock and what is fixed

Both sides run on the same fake clock: `time.monotonic()` = 1000.0 + `at`, `time.time()` =
1800000000.0 + `at`. The start-up token is 1000000 + n for the n-th one drawn (the first at
start-up), Core's own boot id `core-boot-0001`. Nothing else in B1 draws a random number on these
paths.

**Hash order.** `python3 -I` ignores `PYTHONHASHSEED`, so string hashing is randomised per run and
a `set`/`frozenset` of strings iterates in a different order each time. Today no path that reaches a
transcript iterates a set (checked in review; `deck_listeners` sorts, `MOTION_OWN_HOSTS` is only
tested for membership). A new scenario must keep it so: recording twice and `cmp` is the check —
a difference there is a set on an output path, and the recorder must sort it, not the scenario.

## What the recorder patches in B1, and why

The export is never edited; `tools/parity/scenario_core.py` patches the loaded module from
outside: the clock; `a3_core_gate.secrets` (the token); `new_boot_id`; `print` (the journal);
`SimpleUDPClient` and `WatchedClient` (recorders — B1's own `send_message` still runs: traffic,
send failures); `threading.Thread` in the module (started and joined at once); the `threading.Timer`
of `a3_core_state` (due on the scenario clock, fired by `tick`); `SeenFile.follow`'s thread (not
started; its look runs at `tick`). The main block is not run as such: its engine-mode start-up — the
part that is `Core::start` — is copied into `scenario_core.Recording.start` and its dispatcher maps
into `build_dispatcher`, each with B1's SHA and line range. The service's part of the main block
(`audio engine: engine`, sockets, window, network problems, user files, announce) is Task 20's and
is in no transcript.

## Known differences, by decision

- An **evening value that is a list** for an address Core replays (only a hand-edited
  `evening.json` has one): B1 replays it as several arguments, the C++ `replayable` drops it (its
  messages carry one argument). A `null` is replayed as a message without arguments on both sides
  (`desk-first-over-replay`).

- A **list-shaped** `state.json` (e.g. `[1, 2]`): B1 raises at import (`apply_stems` calls `.get`
  on it) and Core does not come up; the C++ Core reads it as no state and no stems. Left as is — a
  broken file is an empty state, never a crash (Review Focus 3); no scenario records it, since B1
  cannot.

## What the C++ Core does not do yet

`a3core::Core` (src/a3-core/Core.cpp) carries only what a recorded scenario pins. Every other path
is a `notPortedYet` stub that throws a `std::logic_error` naming the B1 function and the task that
ports it, test-first, with its scenario. None is left: Task 21 ported `statusJson` (B1's `engine_status`)
and added `rigJson` (project 2's RIG STATE data, no B1 oracle) in src/a3-core/CoreView.cpp. Both, and
`publishView` and `sendFromBench`, belong to the serve thread and throw anywhere else; the window reads
the copy `publishView` leaves in `Core::view()`.

Not a stub, a path that does not exist yet: `handleVu` forwards every datagram to
`vu_destinations()` (the remote Motion followed and every desk), as B1 does with the analyzer's and
StemDeck's meters. The engine's meters (P11: an engine datagram also to the truth's `core` routes
that carry vu, each endpoint once) are Task 22's.

Task 18 ported the router (src/a3-core/CoreValues.cpp): the channel, master, filter and aux-return
handlers, `unrouted_handler`, `values_refused`, `relay`/`broadcast` (a list too), `write_the_evening`,
`send_to_engine`, `send_to_deck`, the deck ring, StemDeck's bus reports.

Task 19 ported the rest of the router: who Core follows and tells (src/a3-core/CorePeers.cpp -- the
device and deck hellos, the one Motion and the one StemDeck, the desks and pro-DJ apps, LIVE and its
repeat, the pointers, the beat and the tempo, StemDeck's preview, the meters' destinations, the
silences) and the recall with the engine's replay (src/a3-core/CoreRecall.cpp). Task 18 had ported
the recall, the hellos and the replay early; they moved there unchanged.

**The hello gate opens after Motion is told** (B1: `_hello_gate.greet` after `motion_said_hello`).
On one serve thread nothing reads the gate in between, so no scenario can tell the two orders
apart; `CorePeers.AHelloOpensTheGateOnlyAfterMotionIsTold` holds it with a value delivered while
Core is still saying the whole state to a Motion that moved.

## Hand-written expectations

None yet. A scenario whose path the Python Core does not have (Task 22's
`meters-to-everyone-once`, the engine's meters) gets a hand-written `.expected.json`, listed here
with the reason, and is never re-recorded.
