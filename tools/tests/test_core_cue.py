"""The headphones' crossfade, in the channel buses' sends (template of
2026-10-01): per deck send 3 (pre-fader, the cue) and send 4 (post-fader, the
mix) to enc_phones, and the stems' send 6 to dec_phones as a cue of its own.

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


def levels(cues=(False,) * 4, stem=False, mix=0.0, stem_on=(False,) * 4):
    # `stem` is gone from send_levels (StemDeck's CUE bus is always on the
    # cue side); kept here so the tests still say what they vary.
    return send_levels(list(cues), list(stem_on), mix, UNITY)


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

    def test_stemdecks_cue_bus_is_always_on_the_cue_side(self):
        # StemDeck decides what is on its CUE bus: stem CUE switches and the
        # decks' PHONES buttons; the bus is silent otherwise. So the stems'
        # cue send is not gated here (final review 2026-10-02: PHONES alone
        # never reached the phones).
        for stem in (False, True):
            self.assertAlmostEqual(levels(stem=stem, mix=0.25)["stem"],
                                   math.cos(0.25 * math.pi / 2))

    def test_the_return_is_on_the_mix_side(self):
        # Template of 2026-10-01 23:11: the return reaches enc_phones twice,
        # post-fader (mix) and pre-fader (cue), like a channel bus.
        for mix in (0.0, 0.3, 1.0):
            self.assertAlmostEqual(levels(mix=mix)["return"]["post"],
                                   math.sin(mix * math.pi / 2))

    def test_a_deck_cue_brings_the_return(self):
        # Spec stemdeck-remote: the C field is gone; cueing a deck brings the
        # return (its FX) along on the cue side.
        on = levels(cues=(False, True, False, False), mix=0.25)
        self.assertAlmostEqual(on["return"]["pre"], math.cos(0.25 * math.pi / 2))
        self.assertEqual(levels(stem=True, mix=0.0)["return"]["pre"], 0.0)

    def test_the_knob_is_held_to_its_range(self):
        self.assertAlmostEqual(levels(cues=(True,) * 4, mix=-1)["decks"][0]["pre"], UNITY)
        self.assertAlmostEqual(levels(cues=(True,) * 4, mix=2)["decks"][0]["post"], UNITY)



class AStemIsCuedInStemDeck(unittest.TestCase):
    def test_a_stem_on_the_channel_shuts_its_cue_send(self):
        # Its cue is StemDeck's C (spec desk-stem-selector): not twice.
        out = levels(cues=(True, True, False, False), stem_on=(True, False, False, False), mix=0.0)
        self.assertEqual(out["decks"][0]["pre"], 0.0)
        self.assertAlmostEqual(out["decks"][1]["pre"], UNITY)


if __name__ == "__main__":
    unittest.main()
