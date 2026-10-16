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


class TwoMotions(unittest.TestCase):
    """One Motion at a time (2026-10-14): a second is said once, not followed."""

    def test_a_rival_is_said_in_the_journal(self):
        source = _source_of("motion_said_hello")
        self.assertIn("take_rivals()", source)
        self.assertIn("a second Motion", source)
        self.assertIn("only one may run", source)
        self.assertIn("_motion.followed", source)

    def test_the_rival_is_named_before_the_early_return(self):
        source = _source_of("motion_said_hello")
        self.assertLess(source.index("_motion.hello("), source.index("take_rivals()"))
        self.assertLess(source.index("take_rivals()"), source.index("point_motion_at("))


class TheTruth(unittest.TestCase):
    def test_motion_says_hello(self):
        truth = a3_osc.load(TRUTH)
        self.assertIn("motion", truth.addresses()["device.hello"]["from"])


class StemDeckKeepsARemoteLead(unittest.TestCase):
    """The StemDeck watch knows the rig's own host (2026-10-06). Since
    2026-10-14 it also follows one StemDeck at a time, so a second one never
    takes the lead while the first is alive (test_core_one_stemdeck_wiring)."""

    def test_the_stemdeck_watch_is_told_what_is_local(self):
        source = (Path(__file__).resolve().parents[2]
                  / "platform-config/debian-x86_64/a3-core/home/aaa/.local/bin/a3-core.py").read_text()
        self.assertIn("_stemdeck_watch = HelloWatch(\n    STEMDECK_SILENCE, "
                      "is_local=lambda host: is_this_machine(host, MOTION_OWN_HOSTS),", source)


class TheBeat(unittest.TestCase):
    def test_goes_on_to_the_followed_motion_when_it_is_remote(self):
        handler = _source_of("osc_handler_beat")
        self.assertIn("_motion.forwards_beat()", handler)
        self.assertIn("osc_a3motion.send_message(address", handler)

    def test_goes_on_before_the_tempo_filter_can_return(self):
        # The delay only wants a moved tempo; Motion wants every beat.
        handler = _source_of("osc_handler_beat")
        self.assertLess(handler.index("_motion.forwards_beat()"),
                        handler.index("_tempo.wanted("))

