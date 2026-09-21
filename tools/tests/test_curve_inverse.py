"""Turning a REAPER value back into the A3 value that produced it.

Core's every outgoing value is bent by a curve -- seven of them, and exactly
one value goes out unbent. REAPER's feedback therefore comes back in REAPER's
units, and relaying it to Motion raw would put the knob in the wrong place.

The inverse is possible because the curves are pinned: curves-golden.json has
all eleven at every hundredth, recorded from the running Python. This is the
first thing that characterisation is used for.

Ten of the eleven are monotonic; three of those have plateaus, where one
output came from a range of inputs and the answer can only be that range's
edge. That is stated in the tests rather than smoothed over, because it is
the one place this is lossy and somebody will meet it.
"""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core"
                       / "home/aaa/.local/lib"))

from a3_core_curves import (CurveNotInvertible, LINEAR_MAPS,  # noqa: E402
                            LinearMap, invert, load_curves)

CURVES = json.loads(
    (ROOT / "tools/curve-characterisation/curves-golden.json").read_text())


class ExactWhereItCan(unittest.TestCase):
    def setUp(self):
        self.curves = load_curves(CURVES)

    def test_a_recorded_point_comes_back_exactly(self):
        # slope_volume is a straight halving: 0.4 in gives 0.2 out.
        self.assertAlmostEqual(invert(self.curves["slope_volume"], 0.2),
                               0.4, places=6)

    def test_between_two_points_it_interpolates(self):
        # The table has a hundredth's resolution; a value landing between two
        # of them is not a value the table cannot answer.
        got = invert(self.curves["slope_volume"], 0.2025)
        self.assertGreater(got, 0.40)
        self.assertLess(got, 0.41)

    def test_a_falling_curve_works_the_same_way(self):
        # slope_fx_freq_lopass runs 0.8 down to 0.2.
        got = invert(self.curves["slope_fx_freq_lopass"], 0.8)
        self.assertAlmostEqual(got, 0.0, places=6)

    def test_outside_the_range_it_clamps_rather_than_extrapolates(self):
        # REAPER can hold a value this curve never produces -- somebody moved
        # a fader past where A3 can drive it. The honest answer is the end of
        # the range, not a number off the end of the table.
        self.assertAlmostEqual(invert(self.curves["slope_volume"], 9.0),
                               1.0, places=6)
        self.assertAlmostEqual(invert(self.curves["slope_volume"], -1.0),
                               0.0, places=6)


class TheLossyPart(unittest.TestCase):
    """Where a plateau makes the answer a range."""

    def setUp(self):
        self.curves = load_curves(CURVES)

    def test_a_plateau_answers_with_its_edge_and_says_so(self):
        # slope_eq saturates: from 0.90 in, the output stays 0.6. Asked for
        # 0.6 it gives the lowest input that produces it -- and says it was
        # not sure, because ten inputs produce it.
        #
        # The plateaus are at the *top* of these three curves, not the bottom.
        # The first version of this test assumed the other end and passed for
        # the wrong reason until it was looked at.
        value, exact = invert(self.curves["slope_eq"], 0.6, tell_me=True)
        self.assertAlmostEqual(value, 0.90, places=6)
        self.assertFalse(exact, "a plateau is not an exact answer")

    def test_a_strict_curve_says_it_was_sure(self):
        value, exact = invert(self.curves["slope_volume"], 0.25,
                              tell_me=True)
        self.assertTrue(exact)


class TheOneThatCannot(unittest.TestCase):
    def test_a_curve_returning_two_numbers_is_refused(self):
        # crossfade_gains gives a stereo and a multi gain from one input. One
        # number cannot say which input it came from, and pretending otherwise
        # would be the confident kind of wrong.
        #
        # This named slope_crossfade_gain until 2026-09-21, which was the old
        # constant-power version of the same idea -- pinned in the golden file
        # while nothing called it any more, and not the same arithmetic as the
        # piecewise-linear fade that runs today.
        curves = load_curves(CURVES)
        with self.assertRaises(CurveNotInvertible):
            invert(curves["crossfade_gains"], 0.25)


class TheOnesThatAreNotCurvesAtAll(unittest.TestCase):
    """The encoder pots do not go through a curve.

    a3-core.py sends them with a straight np.interp(v, [0, 1], [0.05, 0.9]).
    Recording that as an eleventh golden curve would be inventing a
    measurement: the golden file holds what the running Python *did*, and
    nothing recorded this because there was nothing to record. So it is
    written down as the arithmetic it is, and inverted as arithmetic.
    """

    def setUp(self):
        self.curves = load_curves(CURVES)

    def test_the_pot_map_arrives_with_the_curves(self):
        # a3-core.py looks everything up in one dict, so this has to be in it
        # or the reverse table's entry would find nothing and give up.
        self.assertIn("linear_enc_pot", self.curves)
        self.assertIsInstance(self.curves["linear_enc_pot"], LinearMap)

    def test_a_mapped_value_comes_back_where_it_started(self):
        pot = self.curves["linear_enc_pot"]
        for a3_value in (0.0, 0.25, 0.5, 0.75, 1.0):
            sent = 0.05 + a3_value * (0.9 - 0.05)
            self.assertAlmostEqual(invert(pot, sent), a3_value, places=6)

    def test_the_ends_are_the_ends(self):
        pot = self.curves["linear_enc_pot"]
        self.assertAlmostEqual(invert(pot, 0.05), 0.0, places=6)
        self.assertAlmostEqual(invert(pot, 0.9), 1.0, places=6)

    def test_outside_the_range_it_clamps_like_a_curve_does(self):
        """np.interp clamps on the way out, so the way back has to clamp too.

        REAPER can hold 0.0 on this parameter -- somebody moved it there --
        and A3 cannot produce it. The end of the range is the honest answer;
        a negative A3 value would be an invention.
        """
        pot = self.curves["linear_enc_pot"]
        self.assertAlmostEqual(invert(pot, 0.0), 0.0, places=6)
        self.assertAlmostEqual(invert(pot, 1.0), 1.0, places=6)

    def test_it_says_it_was_sure(self):
        """No plateaus here: the map is strictly rising, so every answer is
        exact. Pinned because a caller that asks is entitled to a straight
        yes."""
        pot = self.curves["linear_enc_pot"]
        value, exact = invert(pot, 0.5, tell_me=True)
        self.assertTrue(exact)
        self.assertAlmostEqual(value, (0.5 - 0.05) / 0.85, places=6)

    def test_the_table_says_what_the_source_does(self):
        """The one number that has to agree with a3-core.py."""
        self.assertEqual(LINEAR_MAPS["linear_enc_pot"], (0.0, 1.0, 0.05, 0.9))


if __name__ == "__main__":
    unittest.main()
