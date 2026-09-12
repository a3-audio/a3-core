"""Die Überblendung zwischen Stereo- und Multi-Encoder.

One A3 value, two REAPER gains. It reached the code twice -- once under
`fx-send`, which is how the A3 Mixer's pot has always sent it, and once under
`3d`, which is A3 Motion's per-channel pot. The two blocks were written out
line for line the same, and they carry a warning in the source: an earlier
version had the two gains the wrong way round.

A warning is what you write when nothing checks. This checks.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core"
                       / "home/aaa/.local/lib"))

from a3_core_crossfade import crossfade_gains   # noqa: E402


class TheTwoEnds(unittest.TestCase):
    def test_all_the_way_down_is_multi_alone(self):
        stereo, multi = crossfade_gains(0.0)
        self.assertAlmostEqual(stereo, 0.0)
        self.assertAlmostEqual(multi, 0.5)

    def test_all_the_way_up_is_stereo_alone(self):
        stereo, multi = crossfade_gains(1.0)
        self.assertAlmostEqual(stereo, 0.5)
        self.assertAlmostEqual(multi, 0.0)

    def test_which_way_round_it_goes(self):
        """The warning in the source, as a test.

        An earlier gain branch had these the other way round -- turning the
        control up faded *out* the stereo encoder. It reads as a plausible
        sign flip and is the one mistake this function can make.
        """
        low, _ = crossfade_gains(0.25)
        high, _ = crossfade_gains(0.75)
        self.assertLess(low, high, "stereo must rise with the control")

        _, low_multi = crossfade_gains(0.25)
        _, high_multi = crossfade_gains(0.75)
        self.assertGreater(low_multi, high_multi, "multi must fall")


class TheMiddle(unittest.TestCase):
    def test_at_the_middle_both_are_open(self):
        """Halfway is the one place both encoders are at full 0.5. Either
        gain alone cannot tell you that you are here -- which is why this
        cannot be inverted from one number."""
        stereo, multi = crossfade_gains(0.5)
        self.assertAlmostEqual(stereo, 0.5)
        self.assertAlmostEqual(multi, 0.5)

    def test_each_half_holds_one_gain_still(self):
        """The half that makes it not invertible: below the middle the multi
        gain does not move, above it the stereo gain does not. One number
        answers for half the range and is flat over the other half."""
        self.assertAlmostEqual(crossfade_gains(0.1)[1], 0.5)
        self.assertAlmostEqual(crossfade_gains(0.4)[1], 0.5)
        self.assertAlmostEqual(crossfade_gains(0.6)[0], 0.5)
        self.assertAlmostEqual(crossfade_gains(0.9)[0], 0.5)


class OutsideTheRange(unittest.TestCase):
    def test_it_does_not_run_past_the_ends(self):
        """A value outside 0..1 is not A3's, but UDP carries whatever it is
        handed. Neither gain may exceed 0.5 or go below zero -- a gain above
        0.5 is louder than the control can ask for."""
        for value in (-1.0, -0.01, 1.01, 7.0):
            stereo, multi = crossfade_gains(value)
            self.assertGreaterEqual(stereo, 0.0, value)
            self.assertLessEqual(stereo, 0.5, value)
            self.assertGreaterEqual(multi, 0.0, value)
            self.assertLessEqual(multi, 0.5, value)


if __name__ == "__main__":
    unittest.main()
