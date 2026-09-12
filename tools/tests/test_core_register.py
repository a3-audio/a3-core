"""The register as Core serves it: the catalogue, held against the traffic.

The register says what the system can speak. The traffic says what flew. Put
one against the other and the answer is the thing three evenings of this week
were spent finding by hand: an address that exists and has never arrived is a
dead wire.

The matching is here, on Core's side, rather than in the page -- it is
arithmetic, and arithmetic belongs where a test can reach it. There is no
JavaScript engine on this machine.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = (ROOT / "platform-config/debian-x86_64/a3-core"
           / "home/aaa/.local")
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_register import (collapse, counts_of,   # noqa: E402
                             load, matcher, with_counts)


class OneAddressStandsForAllItsNumbers(unittest.TestCase):
    """REAPER alone can report 19,335 addresses, and they are the same few
    shapes with different numbers in them. Collapsing the numbers is what
    makes matching the register against them cheap enough to do at all."""

    def test_a_numbered_segment_becomes_one_number(self):
        self.assertEqual(collapse("/track/17/volume"), "/track/0/volume")

    def test_every_numbered_segment_collapses(self):
        self.assertEqual(collapse("/track/12/fx/3/fxparam/29/value"),
                         "/track/0/fx/0/fxparam/0/value")

    def test_a_word_is_not_a_number(self):
        self.assertEqual(collapse("/master/phones_mix"),
                         "/master/phones_mix")

    def test_a_number_inside_a_word_is_left_alone(self):
        # /MultiEncoder/azimuth0 is one segment and not a number, so there is
        # nothing here to collapse -- the pattern side has to cope instead.
        self.assertEqual(collapse("/MultiEncoder/azimuth0"),
                         "/MultiEncoder/azimuth0")

    def test_counts_of_one_shape_are_added_up(self):
        rows = [{"address": "/track/1/volume", "count": 10},
                {"address": "/track/2/volume", "count": 5},
                {"address": "/master/volume", "count": 2}]
        counts = counts_of(rows)
        self.assertEqual(counts["/track/0/volume"], 15)
        self.assertEqual(counts["/master/volume"], 2)


class WhatATemplateMatches(unittest.TestCase):
    def test_a_literal_matches_itself(self):
        self.assertTrue(matcher("/state/recall")("/state/recall"))
        self.assertFalse(matcher("/state/recall")("/state/recalling"))

    def test_a_placeholder_matches_one_segment(self):
        match = matcher("/channel/{ch}/gain")
        self.assertTrue(match("/channel/0/gain"))
        self.assertFalse(match("/channel/0/1/gain"))

    def test_reapers_wildcard_matches_one_segment(self):
        match = matcher("/track/@/fx/@/fxparam/@/value")
        self.assertTrue(match("/track/0/fx/0/fxparam/0/value"))
        self.assertFalse(match("/track/0/fx/0/value"))

    def test_a_placeholder_inside_a_segment_matches_the_rest_of_it(self):
        # /MultiEncoder/azimuth{ch} -- the number is glued to the word.
        match = matcher("/MultiEncoder/azimuth{ch}")
        self.assertTrue(match("/MultiEncoder/azimuth0"))
        self.assertFalse(match("/MultiEncoder/elevation0"))

    def test_an_osc_wildcard_reaches_across_segments(self):
        # dispatcher.map("/channel/*") is how Core subscribes to the whole
        # branch, so this has to match what arrives underneath it.
        match = matcher("/channel/*")
        self.assertTrue(match("/channel/0/gain"))
        self.assertTrue(match("/channel/0/eq/high"))

    def test_a_wildcard_in_the_middle_matches(self):
        match = matcher("/channel/*/led/*")
        self.assertTrue(match("/channel/2/led/pfl"))
        self.assertFalse(match("/channel/2/pfl"))

    def test_a_regex_character_in_an_address_is_a_character(self):
        # /channel/n/fx-send and SCROLL_X+ both carry one.
        self.assertTrue(matcher("/channel/{ch}/fx-send")("/channel/1/fx-send"))
        self.assertTrue(matcher("/scroll/x/+")("/scroll/x/+"))
        self.assertFalse(matcher("/scroll/x/+")("/scroll/x/"))


class TheCatalogueAgainstTheTraffic(unittest.TestCase):
    def setUp(self):
        self.register = {
            "_comment": "c",
            "devices": ["core", "mixer"],
            "directions": ["in", "out"],
            "entries": [
                {"address": "/channel/{ch}/gain", "device": "mixer",
                 "direction": "in", "source": "a3-mixer.py:62", "note": ""},
                {"address": "/channel/{ch}/4d", "device": "core",
                 "direction": "in", "source": "a3-core.py:577", "note": ""},
            ]}

    def test_an_address_that_arrived_carries_its_count(self):
        counts = counts_of([{"address": "/channel/0/gain", "count": 7},
                            {"address": "/channel/1/gain", "count": 3}])
        entries = with_counts(self.register, counts)["entries"]
        gain = next(e for e in entries if e["address"].endswith("gain"))
        self.assertEqual(gain["count"], 10)
        self.assertTrue(gain["seen"])

    def test_an_address_nobody_sends_is_a_dead_wire(self):
        # /channel/n/4d is the real one: Core listens for it and neither
        # controller has ever sent it.
        entries = with_counts(self.register, counts_of([]))["entries"]
        for item in entries:
            self.assertFalse(item["seen"])
            self.assertEqual(item["count"], 0)

    def test_the_entries_come_through_whole(self):
        entries = with_counts(self.register, counts_of([]))["entries"]
        self.assertEqual(entries[0]["source"], "a3-mixer.py:62")
        self.assertEqual(entries[0]["device"], "mixer")

    def test_the_head_of_the_register_comes_through(self):
        payload = with_counts(self.register, counts_of([]))
        self.assertEqual(payload["devices"], ["core", "mixer"])

    def test_how_many_are_dead_is_counted_once_rather_than_by_the_page(self):
        counts = counts_of([{"address": "/channel/0/gain", "count": 1}])
        payload = with_counts(self.register, counts)
        self.assertEqual(payload["seen"], 1)
        self.assertEqual(payload["unseen"], 1)


class ABrokenFileIsNotAReasonToFall(unittest.TestCase):
    """The same rule a3_core_state.StateFile lives under. The window is a
    convenience; Core makes the sound."""

    def test_a_missing_file_is_an_empty_register(self):
        self.assertEqual(load(Path("/nowhere/osc-register.json"))["entries"],
                         [])

    def test_a_problem_is_said_out_loud_rather_than_hidden(self):
        # An empty register and a missing one look the same to a reader
        # otherwise, and "the register is empty" is a thing the page has to be
        # able to say.
        self.assertTrue(load(Path("/nowhere/osc-register.json"))["problem"])

    def test_half_a_file_is_an_empty_register(self):
        with tempfile.TemporaryDirectory() as folder:
            broken = Path(folder) / "osc-register.json"
            broken.write_text('{"entries": [{"address": "/a"')
            self.assertEqual(load(broken)["entries"], [])
            self.assertTrue(load(broken)["problem"])

    def test_a_file_of_the_wrong_shape_is_an_empty_register(self):
        with tempfile.TemporaryDirectory() as folder:
            alien = Path(folder) / "osc-register.json"
            alien.write_text('[1, 2, 3]')
            self.assertEqual(load(alien)["entries"], [])
            self.assertTrue(load(alien)["problem"])

    def test_an_entry_short_of_a_field_does_not_take_the_rest_with_it(self):
        with tempfile.TemporaryDirectory() as folder:
            older = Path(folder) / "osc-register.json"
            older.write_text(json.dumps(
                {"entries": [{"address": "/a"},
                             {"address": "/b", "device": "mixer",
                              "direction": "in", "source": "x:1",
                              "note": ""}]}))
            entries = load(older)["entries"]
            self.assertEqual([item["address"] for item in entries],
                             ["/a", "/b"])
            self.assertEqual(entries[0]["device"], "")

    def test_the_shipped_register_loads(self):
        payload = load(PACKAGE / "share/a3-core/osc-register.json")
        self.assertEqual(payload["problem"], "")
        self.assertGreater(len(payload["entries"]), 400)


if __name__ == "__main__":
    unittest.main()
