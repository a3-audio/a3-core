"""Not relaying Core's own echo back at Motion.

REAPER confirms everything it is told. Core sends a value, REAPER sends it
straight back, and a wire that relayed that would tell Motion what Motion just
said -- harmless in itself, but the value has been through a curve and back,
and three of the eleven curves have plateaus where the trip is lossy. On one
of those the knob would come back changed. The performer's own hand would
appear to move it.

So Core keeps what it last sent per address and drops the echo. What is left
is what Core did not cause: somebody moving a fader in REAPER, and the whole
state REAPER dumps when the surface reconnects. Both are news.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core"
                       / "home/aaa/.local/lib"))

from a3_core_echo import EchoFilter   # noqa: E402


class WhatComesBackUnchanged(unittest.TestCase):
    def setUp(self):
        self.filter = EchoFilter()

    def test_the_exact_value_just_sent_is_an_echo(self):
        self.filter.sent("/track/9/volume", 0.75)
        self.assertTrue(self.filter.is_echo("/track/9/volume", 0.75))

    def test_a_different_value_on_that_address_is_news(self):
        self.filter.sent("/track/9/volume", 0.75)
        self.assertFalse(self.filter.is_echo("/track/9/volume", 0.80))

    def test_an_address_nothing_was_sent_to_is_news(self):
        self.assertFalse(self.filter.is_echo("/track/9/volume", 0.75))

    def test_a_float_that_came_back_rounded_is_still_an_echo(self):
        # REAPER stores and returns floats of its own; a value can come back a
        # ten-millionth off and still be the same value. Insisting on
        # equality would let every echo through.
        self.filter.sent("/track/9/volume", 0.75)
        self.assertTrue(self.filter.is_echo("/track/9/volume", 0.7500001))

    def test_a_real_but_tiny_change_is_still_news(self):
        # The tolerance must be smaller than a step anybody can make. A
        # fourteen-bit controller's step is about 0.00006.
        self.filter.sent("/track/9/volume", 0.75)
        self.assertFalse(self.filter.is_echo("/track/9/volume", 0.7501))


class OnceOnly(unittest.TestCase):
    """An echo is heard once. What comes after is not."""

    def setUp(self):
        self.filter = EchoFilter()

    def test_the_same_value_arriving_twice_is_news_the_second_time(self):
        # REAPER dumps its whole state when the surface reconnects, including
        # values Core sent long ago. The first arrival is the echo; a second
        # is the dump, and the dump is exactly what recall is made of.
        self.filter.sent("/track/9/volume", 0.75)
        self.assertTrue(self.filter.is_echo("/track/9/volume", 0.75))
        self.assertFalse(self.filter.is_echo("/track/9/volume", 0.75))

    def test_addresses_do_not_shadow_each_other(self):
        self.filter.sent("/track/9/volume", 0.75)
        self.filter.sent("/track/10/volume", 0.75)
        self.assertTrue(self.filter.is_echo("/track/10/volume", 0.75))
        self.assertTrue(self.filter.is_echo("/track/9/volume", 0.75))


if __name__ == "__main__":
    unittest.main()
