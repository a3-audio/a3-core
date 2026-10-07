"""How every display moves a meter bar -- one block in the truth.

Decided 2026-10-07: the desk's displays, the channel and main LEDs, StemDeck's
strips and Motion's meters rise, fall and hold alike. The sources send raw
peaks; the ballistics -- attack, release, peak hold -- are a central parameter
in Core's truth, so a change is made in one place and every device follows it
on the next fingerprint.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
TRUTH = PACKAGE / "usr/share/a3/a3-osc.json"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc   # noqa: E402


def truth_with(meters):
    data = a3_osc.load(TRUTH).data()
    data["meters"] = meters
    return a3_osc.Truth(data)


def truth_without_meters():
    data = a3_osc.load(TRUTH).data()
    data.pop("meters", None)
    return a3_osc.Truth(data)


class TheShippedBlock(unittest.TestCase):
    def test_the_numbers_of_2026_10_07(self):
        meters = a3_osc.load(TRUTH).meters()
        self.assertEqual(meters.attack_ms, 0)
        self.assertEqual(meters.release_db_per_second, 20)
        self.assertEqual(meters.peak_hold_seconds, 1.5)

    def test_it_is_a_top_level_block(self):
        data = a3_osc.load(TRUTH).data()
        self.assertEqual({k: v for k, v in data["meters"].items() if not k.startswith("_")},
                         {"attack_ms": 0, "release_db_per_second": 20,
                          "peak_hold_seconds": 1.5})


class AnOlderTruth(unittest.TestCase):
    """A device's cache from before the block still reads: the defaults are
    the numbers the block was introduced with."""

    def test_no_block_gives_the_defaults(self):
        self.assertEqual(truth_without_meters().meters(), a3_osc.METER_DEFAULTS)

    def test_the_defaults(self):
        self.assertEqual(tuple(a3_osc.METER_DEFAULTS), (0, 20, 1.5))

    def test_a_missing_key_takes_its_default(self):
        meters = truth_with({"release_db_per_second": 30}).meters()
        self.assertEqual(meters, a3_osc.Meters(0, 30, 1.5))

    def test_a_comment_is_not_a_key(self):
        meters = truth_with({"_comment": "why", "peak_hold_seconds": 2}).meters()
        self.assertEqual(meters.peak_hold_seconds, 2)


class AWrongValue(unittest.TestCase):
    def assertRefused(self, meters):
        with self.assertRaises(a3_osc.TruthError):
            truth_with(meters).meters()

    def test_numbers_only(self):
        self.assertRefused({"attack_ms": "0"})
        self.assertRefused({"release_db_per_second": None})
        self.assertRefused({"peak_hold_seconds": [1.5]})

    def test_a_switch_is_not_a_number(self):
        self.assertRefused({"attack_ms": False})
        self.assertRefused({"peak_hold_seconds": True})

    def test_the_release_has_to_fall(self):
        self.assertRefused({"release_db_per_second": 0})
        self.assertRefused({"release_db_per_second": -20})

    def test_no_negative_hold(self):
        self.assertRefused({"peak_hold_seconds": -0.1})

    def test_no_negative_attack(self):
        self.assertRefused({"attack_ms": -1})

    def test_zero_hold_and_zero_attack_stand(self):
        meters = truth_with({"attack_ms": 0, "peak_hold_seconds": 0}).meters()
        self.assertEqual((meters.attack_ms, meters.peak_hold_seconds), (0, 0))

    def test_a_misspelt_key_is_refused(self):
        # A typo would otherwise fall back to the default without a word.
        self.assertRefused({"release_db_per_sec": 30})

    def test_the_block_is_an_object(self):
        self.assertRefused([0, 20, 1.5])


if __name__ == "__main__":
    unittest.main()
