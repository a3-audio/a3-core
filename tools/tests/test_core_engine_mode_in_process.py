# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Core's engine mode, run in-process (engine plan 4, task B4).

Core cannot be started in a test, but its module can be loaded without the
start-up block: the truth and the state directory are pointed at the package
and a temporary directory, every client is replaced by a recorder, and the
handlers are called as the dispatcher would. Nothing is bound and nothing
leaves the process. What test_core_engine_mode_wiring reads off the source,
this runs.
"""

import os
import runpy
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
CORE = PACKAGE / "home/aaa/.local/bin/a3-core.py"
TRUTH = PACKAGE / "usr/share/a3/a3-osc.json"

LOCAL = ("127.0.0.1", 5555)
DECK_A = ("127.0.0.2", 6000)


class Recorder:
    def __init__(self, name):
        self.name = name
        self.sent = []

    def send_message(self, address, value):
        self.sent.append((address, value))


class EngineModeInProcess(unittest.TestCase):
    def setUp(self):
        self.state = tempfile.TemporaryDirectory()
        self.addCleanup(self.state.cleanup)
        saved = {k: os.environ.get(k)
                 for k in ("A3_OSC_TRUTH", "XDG_STATE_HOME", "A3_NETWORK")}
        self.addCleanup(self._restore, saved)
        os.environ["A3_OSC_TRUTH"] = str(TRUTH)
        os.environ["XDG_STATE_HOME"] = self.state.name
        os.environ.pop("A3_NETWORK", None)

        loaded = runpy.run_path(str(CORE), run_name="a3_core_in_process")
        self.core = loaded["send_to_engine"].__globals__
        self.engine = Recorder("engine")
        self.own_port = Recorder("self")
        self.motion = Recorder("motion")
        self.core.update(
            osc_engine=self.engine, self_client=self.own_port,
            subscribers=[Recorder("mixer"), self.motion],
            osc_a3motion=self.motion,
            _engine_mode=True,
            osc_reaper=self.core["SilentClient"]("reaper"),
            osc_dualdelay=self.core["SilentClient"]("dualdelay"),
            _decks=self.core["DeckSelector"](channels=4, decks=2),
            _project=self.core["EngineSession"](
                [("/channel/1/gain", 0.3)], current=self.core["the_live_evening"]))
        self.core["_hello_gate"].trust(LOCAL)
        # Write what is pending now, before the directory goes: no delayed
        # writer may outlive the test.
        for state_file in ("_state_file", "_evening_file"):
            self.addCleanup(self.core[state_file].flush)

    @staticmethod
    def _restore(saved):
        for key, value in saved.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value

    def hello(self, boot_id):
        before = set(threading.enumerate())
        self.core["osc_handler_engine_hello"](LOCAL, "/engine/hello", boot_id)
        for thread in set(threading.enumerate()) - before:
            if thread.name == "a3-engine-replay":
                thread.join(5)

    def channel(self, address, value):
        self.core["osc_handler_channel"](LOCAL, address, value)

    def test_nothing_reaches_the_engine_before_its_hello(self):
        self.channel("/channel/1/gain", 0.5)
        self.core["osc_handler_master"](LOCAL, "/master/volume", 0.7)
        self.assertEqual(self.engine.sent, [])

    def test_the_hello_replays_then_sends_the_token(self):
        self.hello("boot-1")
        self.assertEqual(self.own_port.sent[0], ("/channel/1/gain", 0.3))
        self.assertEqual(self.own_port.sent[-1],
                         ("/state/recall", self.core["_startup_recall"].token))
        self.assertTrue(self.core["_project"].live)

    def test_the_same_boot_id_again_replays_nothing(self):
        self.hello("boot-1")
        told = len(self.own_port.sent)
        self.hello("boot-1")
        self.assertEqual(len(self.own_port.sent), told)

    def test_after_the_hello_values_reach_the_engine(self):
        self.hello("boot-1")
        self.channel("/channel/1/gain", 0.6)
        self.channel("/channel/2/azimuth", 200.0)
        self.assertEqual(self.engine.sent,
                         [("/channel/1/gain", 0.6), ("/channel/2/azimuth", 180.0)])

    def test_the_token_recall_sends_the_state_then_opens(self):
        self.hello("boot-1")
        self.engine.sent.clear()
        token = self.own_port.sent[-1][1]
        self.core["osc_handler_recall"](LOCAL, "/state/recall", token)
        self.assertIn(("/aux-return/source", 1), self.engine.sent)
        self.assertIn(("/channel/1/cue", 0.0), self.engine.sent)
        self.assertEqual(self.engine.sent[-1], ("/engine/open", 1))

    def test_a_device_recall_does_not_open(self):
        self.hello("boot-1")
        self.engine.sent.clear()
        self.core["osc_handler_recall"](LOCAL, "/state/recall", 1)
        self.assertNotIn(("/engine/open", 1), self.engine.sent)

    def test_the_return_follows_the_desks_mode(self):
        self.hello("boot-1")
        self.core["say_the_whole_state"]()
        self.engine.sent.clear()
        stems = self.core["_stems"]
        stems.turn(self.core["RETURN"], 1)     # STEM -> ANALOG on the ring
        self.core["osc_handler_aux_return"](LOCAL, "/aux-return/stem/push", 1)
        self.assertEqual(self.engine.sent, [("/aux-return/source", 0)])

    def stem_bus_commands(self):
        """What StemDeck is told after a report that breaks the rules (deck
        1's stem 1 on buses 2 and 3, foreign to it), the tidy, and a push of
        the return's mode."""
        stemdeck = Recorder("stemdeck")
        self.core["_stemdeck_client"] = stemdeck
        self.core["_stems"].report(self.core["pair_of"](1, 1), 0b0110)
        self.core["_tidy_settle"].poke(0.0)
        self.core["tidy_when_settled"]()
        self.core["osc_handler_aux_return"](LOCAL, "/aux-return/stem/push", 1)
        return stemdeck.sent

    def test_reaper_mode_tidies_stemdecks_buses(self):
        # The control: the same steps do command StemDeck in REAPER mode.
        self.core["_engine_mode"] = False
        self.assertNotEqual(self.stem_bus_commands(), [])

    def test_no_stem_bus_command_reaches_stemdeck(self):
        self.assertEqual(self.stem_bus_commands(), [])

    def test_a_deck_on_a_channel(self):
        self.hello("boot-1")
        self.core["say_the_whole_state"]()
        deck = Recorder("stemdeck-A")
        self.core["osc_handler_deck_hello"](DECK_A, "/stemdeck/1/hello", "x")
        self.core["_deck_clients"][1] = deck
        self.channel("/channel/3/3d", 0.4)
        self.engine.sent.clear()
        self.channel("/channel/3/stem/turn", 1)
        self.channel("/channel/3/stem/push", 1)
        self.assertEqual(self.engine.sent, [("/channel/3/source", 1)])
        self.assertEqual(deck.sent, [("/stemdeck/1/3d", 0.4)])
        self.assertIn(("/channel/3/source", 1), self.motion.sent)

        self.engine.sent.clear()
        deck.sent.clear()
        self.channel("/channel/3/3d", 0.9)
        self.channel("/channel/3/azimuth", 10.0)
        self.assertEqual(self.engine.sent, [])
        self.assertEqual(deck.sent, [("/stemdeck/1/3d", 0.9)])


if __name__ == "__main__":
    unittest.main()
