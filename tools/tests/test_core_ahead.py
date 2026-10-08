"""StemDeck's preview of the music reaches Motion through Core.

StemDeck sends /stemdeck/ahead section next bars energy on every downbeat to
Core, because Core is the one that knows which Motion is listening. Core
passes it on unchanged, to Motion only, and never notes or replays it: a
preview is about the next bars, and Motion counts an old one as absent.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc   # noqa: E402
from a3_core_ahead import ahead_arguments, from_the_followed   # noqa: E402

CORE = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()
TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


def _body(name):
    """The source of one top-level function of a3-core.py."""
    return CORE.split(f"def {name}(", 1)[1].split("\ndef ", 1)[0]


class TheWord(unittest.TestCase):
    def test_it_is_in_the_truth(self):
        self.assertEqual(TRUTH.address("stemdeck.ahead"), "/stemdeck/ahead")
        entry = TRUTH.addresses()["stemdeck.ahead"]
        self.assertEqual(entry["args"], "ssif")
        self.assertEqual(entry["from"], ["stemdeck"])
        self.assertEqual(entry["to"], ["core"])
        self.assertEqual(entry["relayed_to"], ["motion"])

    def test_core_takes_it_apart_by_its_name(self):
        self.assertEqual(TRUTH.match("/stemdeck/ahead"), ("stemdeck.ahead", {}))

    def test_the_routes_it_travels_exist(self):
        routes = {(route["from"], route["to"]) for route in TRUTH.routes()}
        self.assertIn(("stemdeck", "core.osc"), routes)
        self.assertIn(("core", "motion.osc"), routes)


class WhatGoesOn(unittest.TestCase):
    def test_a_preview_goes_on_as_it_came(self):
        self.assertEqual(ahead_arguments(("build", "drop", 4, 0.5)),
                         ["build", "drop", 4, 0.5])

    def test_none_goes_on_too(self):
        self.assertEqual(ahead_arguments(("none", "none", 0, 0.0)),
                         ["none", "none", 0, 0.0])

    def test_a_whole_number_energy_goes_on_as_a_float(self):
        arguments = ahead_arguments(("drop", "end", 8, 1))
        self.assertEqual(arguments, ["drop", "end", 8, 1.0])
        self.assertIsInstance(arguments[3], float)

    def test_anything_else_is_not_passed_on(self):
        for wrong in ((), ("build", "drop", 4), ("build", "drop", 4, 0.5, 1),
                      (1, "drop", 4, 0.5), ("build", None, 4, 0.5),
                      ("build", "drop", 4.5, 0.5), ("build", "drop", True, 0.5),
                      ("build", "drop", 4, "loud"), ("build", "drop", 4, False)):
            with self.subTest(arguments=wrong):
                self.assertIsNone(ahead_arguments(wrong))


class WhoMaySayIt(unittest.TestCase):
    """Two StemDecks can run at once (the rig's own and a3nuc2's); Motion
    hears the preview of the one Core follows, not a mix of both."""

    def test_the_stemdeck_core_follows(self):
        self.assertTrue(from_the_followed("192.168.8.20", "192.168.8.20"))

    def test_not_another_one(self):
        self.assertFalse(from_the_followed("127.0.0.1", "192.168.8.20"))

    def test_not_while_core_follows_none(self):
        self.assertFalse(from_the_followed("192.168.8.20", None))


class CoreIsWired(unittest.TestCase):
    def test_the_stemdeck_handler_passes_it_on(self):
        body = _body("osc_handler_stemdeck")
        self.assertIn('key == "stemdeck.ahead"', body)
        self.assertIn("forward_ahead(", body)

    def test_only_from_the_followed_stemdeck(self):
        body = _body("osc_handler_stemdeck")
        self.assertIn("from_the_followed(client_address[0], _stemdeck_watch.host)", body)

    def test_it_goes_to_motion_only(self):
        body = _body("forward_ahead")
        self.assertIn("osc_a3motion.send_message(", body)
        self.assertNotIn("subscribers", body)
        self.assertNotIn("osc_a3mixer", body)

    def test_it_is_never_noted_and_so_never_replayed(self):
        body = _body("forward_ahead")
        self.assertNotIn("note_passed_on(", body)
        self.assertNotIn("_relayed", body)
        self.assertNotIn("relay(", body)

    def test_a_malformed_one_is_shown_as_unknown(self):
        self.assertIn("traffic.unknown(", _body("forward_ahead"))


if __name__ == "__main__":
    unittest.main()
