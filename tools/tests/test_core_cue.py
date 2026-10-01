"""The headphones' crossfade, in the channel buses' sends (template of
2026-10-01): per deck send 3 (pre-fader, the cue) and send 4 (post-fader, the
mix) to dec_phones, and the stems' send 6 to it as a cue of its own.

The phones-mix knob fades cue (left) into mix (right) at constant power. A
deck's cue send only opens while its cue is on; its mix send follows the knob
alone. The stem cue behaves like a deck's cue."""

import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_cue import send_levels   # noqa: E402

UNITY = 1.0


def levels(cues=(False,) * 4, stem=False, mix=0.0):
    return send_levels(list(cues), stem, mix, UNITY)


class TheKnob(unittest.TestCase):
    def test_full_left_is_the_cue_alone(self):
        out = levels(cues=(True, False, False, False), mix=0.0)
        self.assertAlmostEqual(out["decks"][0]["pre"], UNITY)
        self.assertAlmostEqual(out["decks"][0]["post"], 0.0)

    def test_full_right_is_the_mix_alone(self):
        out = levels(cues=(True, False, False, False), mix=1.0)
        self.assertAlmostEqual(out["decks"][0]["pre"], 0.0)
        for deck in out["decks"]:
            self.assertAlmostEqual(deck["post"], UNITY)

    def test_the_middle_is_constant_power(self):
        out = levels(cues=(True,) * 4, mix=0.5)
        pre, post = out["decks"][0]["pre"], out["decks"][0]["post"]
        self.assertAlmostEqual(pre ** 2 + post ** 2, UNITY ** 2)
        self.assertAlmostEqual(pre, post)


class TheCues(unittest.TestCase):
    def test_a_deck_without_cue_has_its_cue_send_shut(self):
        out = levels(cues=(False, True, False, False), mix=0.0)
        self.assertEqual(out["decks"][0]["pre"], 0.0)
        self.assertAlmostEqual(out["decks"][1]["pre"], UNITY)

    def test_the_mix_does_not_care_about_the_cue(self):
        a = levels(cues=(True, False, False, False), mix=0.3)
        self.assertAlmostEqual(a["decks"][0]["post"], a["decks"][1]["post"])

    def test_the_stem_cue_is_like_a_deck_cue(self):
        on = levels(stem=True, mix=0.25)
        self.assertAlmostEqual(on["stem"], math.cos(0.25 * math.pi / 2))
        self.assertEqual(levels(stem=False, mix=0.0)["stem"], 0.0)

    def test_the_knob_is_held_to_its_range(self):
        self.assertAlmostEqual(levels(cues=(True,) * 4, mix=-1)["decks"][0]["pre"], UNITY)
        self.assertAlmostEqual(levels(cues=(True,) * 4, mix=2)["decks"][0]["post"], UNITY)


if __name__ == "__main__":
    unittest.main()
