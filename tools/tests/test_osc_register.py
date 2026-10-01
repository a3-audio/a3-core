"""The register -- what the system can speak -- is built from the one truth.

It used to be lifted out of six places in four repos where addresses were
written, and a test rebuilt it from the neighbouring checkouts or skipped
where one was missing. Since 2026-09-30 every address is written once, in
a3-osc.json, so the register is a view of that file and its drift test needs
nothing beside this checkout.
"""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT.parent / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

import a3_osc          # noqa: E402
import osc_register as reg   # noqa: E402

TRUTH = a3_osc.load(ROOT.parent / reg.TRUTH)


def entries_for(address):
    return {(e["device"], e["direction"])
            for e in reg.build(TRUTH)["entries"] if e["address"] == address}


class WhatCoreHearsAndSays(unittest.TestCase):
    def test_a_fader_comes_in_from_both_desks_and_goes_back_out(self):
        # mixer and motion send it to Core, Core relays it to both
        self.assertEqual(entries_for("/channel/{ch}/volume"),
                         {("mixer", "both"), ("motion", "both")})

    def test_a_lamp_only_goes_out(self):
        self.assertEqual(entries_for("/channel/{ch}/cue/led"),
                         {("mixer", "out"), ("motion", "out")})

    def test_the_position_only_comes_in(self):
        self.assertEqual(entries_for("/channel/{ch}/azimuth"), {("motion", "in")})

    def test_what_passes_core_by_is_aside(self):
        self.assertIn(("motion", "aside"), entries_for("/vu/{n}"))
        self.assertIn(("mixer", "aside"), entries_for("/vu/{n}"))
        self.assertIn(("beat-analyzer", "aside"), entries_for("/tap"))

    def test_every_address_of_ours_is_in_it(self):
        listed = {e["address"] for e in reg.build(TRUTH)["entries"]}
        for key in TRUTH.addresses():
            self.assertIn(TRUTH.pattern(key), listed, key)

    def test_the_words_we_do_not_own_are_in_it_too(self):
        listed = {e["address"] for e in reg.build(TRUTH)["entries"]}
        for block in TRUTH.external().values():
            for pattern in block["patterns"]:
                self.assertIn(pattern, listed)

    def test_an_entry_says_where_it_is_written(self):
        (entry,) = [e for e in reg.build(TRUTH)["entries"]
                    if e["address"] == "/master/aux-return" and e["device"] == "mixer"]
        self.assertEqual(entry["source"], "a3-osc.json: master.aux-return")
        self.assertTrue(entry["note"])


class TheCheckedInRegister(unittest.TestCase):
    """Generated and checked in, because the installed Core reads a finished
    file; this is what stops it going stale."""

    def test_it_is_what_the_truth_says_today(self):
        shipped = (ROOT.parent / reg.REGISTER).read_text()
        self.assertEqual(reg.as_text(reg.build(TRUTH)), shipped,
                         "share/a3-core/osc-register.json no longer matches "
                         "a3-osc.json -- run tools/osc_register.py and commit")


if __name__ == "__main__":
    unittest.main()
