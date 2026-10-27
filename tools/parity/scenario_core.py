# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""B1's a3-core.py, run in-process on a scripted scenario, everything it does written down.

Loaded as B1's tools/tests/test_core_engine_mode_in_process.py loads it (runpy, run_name
"a3_core_in_process", so the `if __name__ == "__main__"` block does not run), then the main
block's engine-mode start-up is run here, copied from B1. Nothing is bound and nothing leaves the
process: every client is a recorder that builds the datagram with python-osc exactly as
SimpleUDPClient does and writes down what it would have sent.

What is patched from the outside (the export is never edited), and why:

- time.monotonic / time.time: the scenario's clock (MONOTONIC_BASE / WALL_BASE + step "at").
- a3_core_gate.secrets.randbelow: the start-up token is TOKEN_BASE + n, n = 1, 2, ...
- new_boot_id: Core's own boot id is fixed (only the announce thread reads it, which is not run).
- builtins.print: the journal (stdout or stderr, by `file=`).
- SimpleUDPClient and WatchedClient (module globals): recorders. WatchedClient keeps B1's own
  send_message (echo filter, traffic OUT, send failures); only the innermost socket send is
  replaced, so what is recorded is what python-osc would put on the wire.
- threading.Thread (module global): started and joined at once -- the engine replay thread runs
  to its end inside the hello handler, as the C++ Core queues it on the serve thread (P8).
- a3_core_state.threading.Timer: the debounced writes of state.json / evening.json fall due on
  the scenario clock and are written at the next tick at or after remember + 2 s.
- SeenFile.follow's polling thread is not started: its look is run at each tick instead, every
  5 s on the scenario clock (the C++ SeenFile::writeIfDue).
"""

import base64
import builtins
import json
import os
import runpy
import sys
import tempfile
import threading
import time
from pathlib import Path

MONOTONIC_BASE = 1000.0
WALL_BASE = 1800000000.0
TOKEN_BASE = 1000000
CORE_BOOT_ID = "core-boot-0001"
#: The socket Core's replays leave from (the main block binds an ephemeral one and trusts it).
SELF_SOCKET = ("127.0.0.1", 50000)
STATE_FILES = ("state.json", "evening.json", "seen.json")
STATE_MARK = "<state>"


class ScenarioError(Exception):
    pass


class Clock:
    def __init__(self):
        self.at = 0.0

    def monotonic(self):
        return MONOTONIC_BASE + self.at

    def wall(self):
        return WALL_BASE + self.at


def osc_args(dgram):
    """The arguments of a datagram as [tag, value] pairs, read off the wire: python-osc's own
    choice of tag, and a float as the float32 it became."""
    from pythonosc.osc_message import OscMessage
    message = OscMessage(dgram)
    tags = _tags(dgram)
    params = list(message.params)
    out = []
    for tag in tags:
        if tag in "TFN":
            if tag in "TF" and params and isinstance(params[0], bool):
                params.pop(0)
            elif tag == "N" and params and params[0] is None:
                params.pop(0)
            out.append([tag, {"T": True, "F": False, "N": None}[tag]])
            continue
        value = params.pop(0)
        if tag == "b":
            value = base64.b64encode(value).decode("ascii")
        out.append([tag, value])
    return out


def _tags(dgram):
    end = dgram.index(b"\0")
    start = (end + 4) & ~3
    stop = dgram.index(b"\0", start)
    tags = dgram[start:stop].decode("ascii")
    if not tags.startswith(","):
        raise ScenarioError(f"no type tags in {dgram!r}")
    return tags[1:]


def build_message(address, args):
    """A datagram from a scenario's [tag, value] pairs, encoded by python-osc."""
    from pythonosc.osc_message_builder import OscMessageBuilder
    builder = OscMessageBuilder(address=address)
    for tag, value in args:
        if tag in "TF":
            builder.add_arg(tag == "T", tag)
        elif tag == "N":
            builder.add_arg(None, tag)
        elif tag == "b":
            builder.add_arg(base64.b64decode(value), tag)
        else:
            builder.add_arg(value, tag)
    return builder.build().dgram


class ThreadingShim:
    """The threading module, but for one or two names."""

    def __init__(self, **overrides):
        self._overrides = overrides

    def __getattr__(self, name):
        if name in self._overrides:
            return self._overrides[name]
        return getattr(threading, name)


class JoinedThread(threading.Thread):
    def start(self):
        super().start()
        self.join()


class Recording:
    """One scenario's run: B1 loaded, the start-up done, the steps fed in, the transcript kept."""

    def __init__(self, baseline, scenario):
        self.baseline = Path(baseline)
        self.scenario = scenario
        self.clock = Clock()
        self.transcript = []
        self.to_self = []
        self.tokens = 0
        self.seen_next_look = None

    # ------------------------------------------------------------ the record

    def record_send(self, name, host, port, dgram):
        from pythonosc.osc_message import OscMessage
        self.transcript.append({"at": self.clock.at, "to": name, "host": host, "port": port,
                                "address": OscMessage(dgram).address, "args": osc_args(dgram)})

    def record_journal(self, *args, sep=" ", end="\n", file=None, flush=False):
        line = (" " if sep is None else sep).join(str(arg) for arg in args)
        line = line.replace(str(self.state_dir), STATE_MARK)
        self.transcript.append({"at": self.clock.at, "journal": line,
                                "stderr": file is sys.stderr})

    def record_file(self, path):
        self.transcript.append({"file": Path(path).name,
                                "json": json.loads(Path(path).read_text())})

    # ------------------------------------------------------------ set-up

    def write_files(self):
        self.state_dir.mkdir(parents=True)
        for name, content in self.scenario.get("files", {}).items():
            if name not in STATE_FILES:
                raise ScenarioError(f"files: {name} is none of {STATE_FILES}")
            text = content if isinstance(content, str) else json.dumps(content, indent=2) + "\n"
            (self.state_dir / name).write_text(text)

    def load(self):
        package = self.baseline / "platform-config/debian-x86_64/a3-core"
        os.environ["A3_OSC_TRUTH"] = str(package / "usr/share/a3/a3-osc.json")
        os.environ["XDG_STATE_HOME"] = str(self.state_dir.parent)
        os.environ.pop("A3_NETWORK", None)
        time.monotonic = self.clock.monotonic
        time.time = self.clock.wall
        builtins.print = self.record_journal
        sys.path.insert(0, str(package / "home/aaa/.local/lib"))

        import a3_core_gate
        recording = self

        class FixedSecrets:
            @staticmethod
            def randbelow(_limit):
                recording.tokens += 1
                return TOKEN_BASE + recording.tokens - 2

        a3_core_gate.secrets = FixedSecrets
        loaded = runpy.run_path(str(package / "home/aaa/.local/bin/a3-core.py"),
                                run_name="a3_core_in_process")
        self.core = loaded["send_to_engine"].__globals__

    def patch(self):
        core = self.core
        recording = self
        from pythonosc.udp_client import SimpleUDPClient

        class RecordingUDPClient(SimpleUDPClient):
            def __init__(self, address, port, allow_broadcast=False, family=None):
                self._address, self._port = address, port
                self.recorded_as = None

            def send(self, content):
                recording.record_send(self.recorded_as, self._address, self._port, content.dgram)

        class SelfClient(RecordingUDPClient):
            def send(self, content):
                recording.record_send("self", self._address, self._port, content.dgram)
                recording.to_self.append(content.dgram)

        class RecordingWatchedClient(core["WatchedClient"]):
            def __init__(self, client, name):
                super().__init__(client, name)
                client.recorded_as = name

        self.SelfClient = SelfClient
        core["SimpleUDPClient"] = RecordingUDPClient
        core["WatchedClient"] = RecordingWatchedClient
        core["threading"] = ThreadingShim(Thread=JoinedThread)
        core["new_boot_id"] = lambda: CORE_BOOT_ID

        import a3_core_state

        class FakeTimer:
            def __init__(self, delay, function):
                self.due = recording.clock.monotonic() + delay
                self.function = function
                self.cancelled = False
                self.daemon = True

            def start(self):
                pass

            def cancel(self):
                self.cancelled = True

        a3_core_state.threading = ThreadingShim(Timer=FakeTimer)
        self.FakeTimer = FakeTimer

        for attribute in ("_state_file", "_evening_file"):
            self._record_writes(core[attribute], "flush", core[attribute]._path)
        self._record_writes(core["_seen_file"], "write", core["_seen_file"]._path)

    def _record_writes(self, owner, method, path):
        original = getattr(owner, method)

        def recorded():
            before = owner.writes
            original()
            if owner.writes != before:
                self.record_file(path)

        setattr(owner, method, recorded)

    # ------------------------------------------------------------ the main block

    def start(self):
        """B1's main block (a3-core 7a7b2441f74e, a3-core.py:2173-2614), engine mode: the part the
        C++ Core::start owns. The service's part -- the argument parser, `audio engine: engine`,
        the sockets, the window, the announce thread, the user files -- is not run here and not
        in the transcript (Task 20)."""
        core = self.core
        truth = core["_truth"]
        WatchedClient, SimpleUDPClient = core["WatchedClient"], core["SimpleUDPClient"]
        given = self.scenario.get("args", {})
        mixer = given.get("mixer", f"{core['A3MIXER_HOST']}:{core['A3MIXER_PORT']}")
        motion = given.get("motion", f"{core['A3MOTION_HOST']}:{core['A3MOTION_PORT']}")
        reaper = f"{core['REAPER_HOST']}:{core['REAPER_PORT']}"
        port = core["OSC_PORT_CORE"]

        core["_print_osc"] = False
        core["_engine_mode"], core["_engine_reason"] = True, ""
        core["PEER_HOSTS"] = {"mixer": mixer.rpartition(":")[0],
                              "motion": motion.rpartition(":")[0],
                              "reaper": reaper.rpartition(":")[0]}
        core["_hello_gate"].trust_host(core["PEER_HOSTS"]["mixer"])
        for name, spec in (("mixer", mixer), ("motion", motion)):
            host, _, client_port = spec.rpartition(":")
            client = WatchedClient(SimpleUDPClient(host, int(client_port)), name)
            core["osc_a3mixer" if name == "mixer" else "osc_a3motion"] = client
        core["osc_reaper"] = core["SilentClient"]("reaper")
        core["osc_dualdelay"] = core["SilentClient"]("dualdelay")
        core["udp_clients_iem"] = tuple(core["SilentClient"]("iem") for _ in core["udp_clients_iem"])
        core["_decks"] = core["DeckSelector"](
            channels=core["_layout"].channel_count,
            decks=truth.addresses()["stemdeck.deck-hello"]["deck"][1])
        core["osc_engine"] = WatchedClient(SimpleUDPClient(*truth.endpoint("engine", "osc")), "engine")
        core["subscribers"] = [core["osc_a3mixer"], core["osc_a3motion"]]
        motion_host, _, motion_port = motion.rpartition(":")
        core["_motion"] = core["MotionTarget"]((motion_host, int(motion_port)), core["A3MOTION_PORT"],
                                               core["MOTION_OWN_HOSTS"] | {motion_host},
                                               truth.port("motion", "vu"))
        extra = core["parse_subscribers"](given.get("subscriber", []),
                                          reserved=core["SHIPPED"] + ("reaper", "iem", "dualdelay"))
        for name, host, subscriber_port in extra:
            core["subscribers"].append(WatchedClient(SimpleUDPClient(host, subscriber_port), name))
            core["PEER_HOSTS"][name] = host
            print(f"subscriber {name} at {host}:{subscriber_port}")

        self.dispatcher = self.build_dispatcher()

        evening = list(core["replayable"](truth, core["_evening_file"].load()))
        core["_project"] = core["EngineSession"](evening, current=core["the_live_evening"])
        core["_decks"].from_evening(core["_evening_file"].load())

        core["speak_remembered_state"]()

        print(f"{core['_seen_file'].restore()} addresses remembered from {core['state_path']()}")
        self.seen_next_look = self.clock.monotonic() + core["_seen_file"]._delay

        self_client = self.SelfClient(truth.host("local"), port)
        core["self_client"] = self_client
        core["_hello_gate"].trust(SELF_SOCKET)
        core["_subscribers_owner"].claim()

    def build_dispatcher(self):
        """B1's maps (a3-core 7a7b2441f74e, a3-core.py:2297-2342), as the main block builds them."""
        from pythonosc import dispatcher as osc_dispatcher
        core = self.core
        truth = core["_truth"]
        dispatcher = osc_dispatcher.Dispatcher()
        for family, handler in (("channel", core["osc_handler_channel"]),
                                ("master", core["osc_handler_master"]),
                                ("filter", core["osc_handler_filter"]),
                                ("aux-return", core["osc_handler_aux_return"]),
                                ("stemdeck", core["osc_handler_stemdeck"])):
            dispatcher.map(f"/{family}/*", handler, needs_reply_address=True)
        dispatcher.map(core["OSC_ADDRESS_RECALL"], core["osc_handler_recall"],
                       needs_reply_address=True)
        dispatcher.map(core["OSC_ADDRESS_BEAT"], core["osc_handler_beat"],
                       needs_reply_address=True)
        dispatcher.map(truth.address("device.hello"), core["osc_handler_device_hello"],
                       needs_reply_address=True)
        dispatcher.map(truth.address("engine.hello"), core["osc_handler_engine_hello"],
                       needs_reply_address=True)
        first_deck, last_deck = truth.addresses()["stemdeck.deck-hello"]["deck"]
        for deck in range(first_deck, last_deck + 1):
            dispatcher.map(truth.address("stemdeck.deck-hello", deck=deck),
                           core["osc_handler_deck_hello"], needs_reply_address=True)
        dispatcher.map(truth.address("prodj.live"), core["osc_handler_prodj_live"],
                       needs_reply_address=True)
        for pointer in ("stemdeck.pointer", "motion.pointer"):
            dispatcher.map(truth.address(pointer), core["osc_handler_pointer"],
                           needs_reply_address=True)
        dispatcher.set_default_handler(core["unrouted_handler"], needs_reply_address=True)
        return dispatcher

    # ------------------------------------------------------------ the steps

    def handle(self, dgram, client):
        self.dispatcher.call_handlers_for_packet(dgram, client)

    def pass_of(self, packets):
        """One pass of B1's serve loop (a3_core_latest.serve): the newest position wins."""
        core = self.core
        latest = core["position_key"].__globals__["latest_wins"]
        keep = latest(packets, lambda data: core["position_key"](data, core["is_position"]))
        for data, client in keep:
            self.handle(data, client)

    def feed_back_own_port(self):
        while self.to_self:
            waiting, self.to_self = self.to_self, []
            self.pass_of([(data, SELF_SOCKET) for data in waiting])

    def tick(self):
        core = self.core
        core["on_tick"]()
        now = self.clock.monotonic()
        for attribute in ("_state_file", "_evening_file"):
            timer = core[attribute]._timer
            if isinstance(timer, self.FakeTimer) and not timer.cancelled and now >= timer.due:
                timer.function()
        self.seen_look(now)

    def seen_look(self, now):
        """SeenFile._watch's body, at its 5 s look (a3_core_seen, B1)."""
        seen = self.core["_seen_file"]
        if now < self.seen_next_look:
            return
        self.seen_next_look = now + seen._delay
        created = seen._traffic.created()
        settled = created == seen._polled
        seen._polled = created
        if settled and created != seen._written_at_created:
            seen.write()

    def flush(self):
        """B1's stop(): the state file, the evening, the address list (a3-core.py:2463-2471)."""
        core = self.core
        core["_state_file"].flush()
        core["_evening_file"].flush()
        core["_seen_file"].write()

    def vu(self, dgram):
        """The vu-relay port (a3_core_vu_relay.forward): the bytes, unchanged, to every
        vu_destination."""
        for host, port in self.core["vu_destinations"]():
            self.transcript.append({"at": self.clock.at, "to": "vu", "host": host, "port": port,
                                    "raw": base64.b64encode(dgram).decode("ascii")})

    def step(self, step):
        at = float(step["at"])
        if at < self.clock.at:
            raise ScenarioError(f"step at {at} before {self.clock.at}")
        self.clock.at = at
        client = tuple(step["from"]) if "from" in step else None
        if "osc" in step:
            address, *args = step["osc"]
            self.pass_of([(build_message(address, args), client)])
        elif "packet" in step:
            self.pass_of([(base64.b64decode(step["packet"]), client)])
        elif "vu" in step:
            self.vu(base64.b64decode(step["vu"]))
        elif step.get("tick"):
            self.tick()
        elif step.get("flush"):
            self.flush()
        else:
            raise ScenarioError(f"a step that does nothing: {step}")
        self.feed_back_own_port()

    def run(self):
        saved_print, saved_monotonic, saved_time = builtins.print, time.monotonic, time.time
        with tempfile.TemporaryDirectory() as temporary:
            self.state_dir = Path(temporary) / "a3-core"
            try:
                self.write_files()
                self.load()
                self.patch()
                self.start()
                self.feed_back_own_port()
                for step in self.scenario.get("steps", []):
                    self.step(step)
            finally:
                builtins.print, time.monotonic, time.time = saved_print, saved_monotonic, saved_time
        return self.transcript


def record(baseline, scenario):
    return Recording(baseline, scenario).run()
