"""The 3D law: how much of the isolated band moves.

One A3 value, two REAPER gains. The steady (multi-enc) gain never moves; the
band (stereo-enc) gain is 3D read as an amplitude, written in PurestGain's
law dB = A*80 - 40. The band and the phase-inverted band subtracted from the
steady bed share that one gain, so band + remainder = input at every 3D.

Decided by the maintainer on 2026-10-06 after F14 (gain-structure notes): the
old crossfade lowered the steady bed above 0.5 and the channel got louder or
quieter as 3D moved.
"""

import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core"
                       / "home/aaa/.local/lib"))

from a3_core_crossfade import crossfade_gains   # noqa: E402

UNITY = 0.5   # PurestGain A for 0 dB


def purest_gain_db(a):
    return a * 80 - 40


class TheSteadyBedNeverMoves(unittest.TestCase):
    def test_multi_is_unity_at_every_position(self):
        for value in (0.0, 0.005, 0.01, 0.1, 0.25, 0.5, 0.75, 0.99, 1.0):
            self.assertAlmostEqual(crossfade_gains(value)[1], UNITY, msg=value)


class TheBandIsTheAmplitude(unittest.TestCase):
    def test_full_3d_is_unity_on_both(self):
        self.assertEqual(crossfade_gains(1.0), (0.5, 0.5))

    def test_half_3d_is_half_the_amplitude(self):
        stereo, multi = crossfade_gains(0.5)
        self.assertAlmostEqual(stereo, 0.42474, places=5)
        self.assertAlmostEqual(purest_gain_db(stereo), -6.0206, places=4)
        self.assertAlmostEqual(multi, 0.5)

    def test_a_tenth_is_minus_twenty_db(self):
        stereo, _ = crossfade_gains(0.1)
        self.assertAlmostEqual(stereo, 0.25)

    def test_a_quarter_is_minus_twelve_db(self):
        stereo, _ = crossfade_gains(0.25)
        self.assertAlmostEqual(stereo, 0.349485, places=6)

    def test_every_position_is_its_amplitude(self):
        for i in range(2, 101):
            x = i / 100
            stereo, _ = crossfade_gains(x)
            self.assertAlmostEqual(10 ** (purest_gain_db(stereo) / 20), x,
                                   places=9, msg=x)

    def test_zero_is_the_floor(self):
        self.assertEqual(crossfade_gains(0.0), (0.0, 0.5))

    def test_at_and_below_minus_forty_db_it_is_the_floor(self):
        """PurestGain cannot go below A 0 (-40 dB): 0.01 lands exactly
        there, and anything quieter clamps to it rather than going negative."""
        self.assertAlmostEqual(crossfade_gains(0.01)[0], 0.0, places=12)
        self.assertEqual(crossfade_gains(0.005)[0], 0.0)
        self.assertEqual(crossfade_gains(1e-12)[0], 0.0)

    def test_it_rises_with_the_control(self):
        gains = [crossfade_gains(i / 100)[0] for i in range(1, 101)]
        self.assertEqual(gains, sorted(gains))


class OutsideTheRange(unittest.TestCase):
    def test_it_does_not_run_past_the_ends(self):
        """UDP carries whatever it is handed. Neither gain may exceed 0.5
        (0 dB) or go below zero."""
        for value in (-1.0, -0.01, 1.01, 7.0, math.inf, -math.inf):
            stereo, multi = crossfade_gains(value)
            self.assertGreaterEqual(stereo, 0.0, value)
            self.assertLessEqual(stereo, 0.5, value)
            self.assertEqual(multi, 0.5, value)

    def test_above_one_is_one(self):
        self.assertEqual(crossfade_gains(3.0), (0.5, 0.5))

    def test_below_zero_is_zero(self):
        self.assertEqual(crossfade_gains(-3.0), (0.0, 0.5))


if __name__ == "__main__":
    unittest.main()
