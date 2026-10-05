"""Core follows the Motion that said hello last (spec devices-and-remote-access).

Read off the source, like test_core_stems_wiring: Core itself cannot be
started in a test. The decisions are tested in test_core_motion_target; this
holds that a3-core.py asks them and acts on the answer.
"""

import ast
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc   # noqa: E402

CORE = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()
TRUTH = PACKAGE / "usr/share/a3/a3-osc.json"


def _source_of(function_name):
    node = next(n for n in ast.walk(ast.parse(CORE))
                if isinstance(n, ast.FunctionDef) and n.name == function_name)
    return ast.get_source_segment(CORE, node)


class AHelloFromMotion(unittest.TestCase):
    def test_is_handed_to_the_target(self):
        self.assertIn("motion_said_hello(", _source_of("osc_handler_device_hello"))
        self.assertIn("_motion.hello(", _source_of("motion_said_hello"))

    def test_its_silence_is_watched_on_every_tick(self):
        """Not only when somebody says hello: with the desk and StemDeck off a
        silent remote Motion would otherwise be followed all night."""
        self.assertIn("tick=on_tick", CORE)
        self.assertIn("notice_motion_silence(", _source_of("on_tick"))
        self.assertIn("tidy_when_settled(", _source_of("on_tick"))
        self.assertIn("_motion.silence(", _source_of("notice_motion_silence"))


class ASwitch(unittest.TestCase):
    def setUp(self):
        self.source = _source_of("point_motion_at")

    def test_takes_motions_slot_among_the_subscribers(self):
        self.assertIn("replace_named(subscribers,", self.source)

    def test_names_the_new_host_motion_in_the_window(self):
        self.assertIn('PEER_HOSTS["motion"]', self.source)

    def test_says_the_whole_state_to_it(self):
        """A Motion that has just come up knows nothing; its own recall may
        have reached Core before its hello and gone to the old target."""
        self.assertIn("say_the_whole_state(", self.source)

    def test_a_recall_says_the_same_whole_state(self):
        self.assertIn("say_the_whole_state(", _source_of("osc_handler_recall"))

    def test_both_the_hello_and_the_silence_switch(self):
        self.assertIn("point_motion_at(", _source_of("motion_said_hello"))
        self.assertIn("point_motion_at(", _source_of("notice_motion_silence"))


class TheTruth(unittest.TestCase):
    def test_motion_says_hello(self):
        truth = a3_osc.load(TRUTH)
        self.assertIn("motion", truth.addresses()["device.hello"]["from"])
