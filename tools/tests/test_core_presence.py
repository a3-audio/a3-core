"""Whether StemDeck is still there, from its hello every 30 s (spec
stemdeck-remote)."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_presence import Presence  # noqa: E402


class AHelloEveryThirtySeconds(unittest.TestCase):
    def test_the_first_hello_is_news(self):
        self.assertTrue(Presence(60).heard(0.0))

    def test_the_next_hello_in_time_is_not(self):
        p = Presence(60)
        p.heard(0.0)
        self.assertFalse(p.heard(30.0))
        self.assertTrue(p.present(80.0))

    def test_silence_is_noticed_once(self):
        p = Presence(60)
        p.heard(0.0)
        self.assertFalse(p.gone(59.0))
        self.assertTrue(p.gone(61.0))
        self.assertFalse(p.gone(90.0))
        self.assertFalse(p.present(61.0))

    def test_a_hello_after_silence_is_news_again(self):
        p = Presence(60)
        p.heard(0.0)
        p.gone(61.0)
        self.assertTrue(p.heard(70.0))

    def test_never_heard_is_not_present_and_never_gone(self):
        p = Presence(60)
        self.assertFalse(p.present(0.0))
        self.assertFalse(p.gone(1000.0))


from a3_core_presence import HelloWatch  # noqa: E402


class WhatAHelloMeansForTheLink(unittest.TestCase):
    """The sequences Core lives through (final review 2026-10-02): when to
    (re)connect and ask StemDeck for everything, and when to forget it."""

    def test_the_first_hello_connects(self):
        w = HelloWatch(60)
        self.assertTrue(w.hello("10.0.0.5", 0.0))
        self.assertEqual(w.host, "10.0.0.5")

    def test_a_hello_in_time_from_the_same_host_does_nothing(self):
        w = HelloWatch(60)
        w.hello("10.0.0.5", 0.0)
        self.assertFalse(w.hello("10.0.0.5", 30.0))

    def test_a_hello_from_another_host_reconnects(self):
        w = HelloWatch(60)
        w.hello("10.0.0.5", 0.0)
        self.assertTrue(w.hello("10.0.0.6", 30.0))
        self.assertEqual(w.host, "10.0.0.6")

    def test_silence_is_noticed_once_and_forgets_the_host(self):
        w = HelloWatch(60)
        w.hello("10.0.0.5", 0.0)
        self.assertFalse(w.silence(30.0))
        self.assertTrue(w.silence(61.0))
        self.assertFalse(w.silence(91.0))
        self.assertIsNone(w.host)

    def test_a_hello_after_silence_connects_again(self):
        w = HelloWatch(60)
        w.hello("10.0.0.5", 0.0)
        w.silence(61.0)
        self.assertTrue(w.hello("10.0.0.5", 90.0))

    def test_a_hello_is_never_silence_at_the_same_moment(self):
        w = HelloWatch(60)
        w.hello("10.0.0.5", 0.0)
        w.silence(61.0)
        w.hello("10.0.0.5", 90.0)
        self.assertFalse(w.silence(90.0))

    def test_no_stemdeck_ever_is_never_silence(self):
        self.assertFalse(HelloWatch(60).silence(1000.0))


class TwoHostsSayingHelloInTurn(unittest.TestCase):
    """One device of a kind at a time (maintainer, 2026-10-05): the one that
    arrived last wins. A hello every 30 s from one that is already there is
    not an arrival -- otherwise two running at once would take the link from
    each other every 30 s, with a recall each time."""

    def test_the_one_that_arrived_first_does_not_take_it_back(self):
        w = HelloWatch(60)
        w.hello("10.0.0.5", 0.0)
        self.assertTrue(w.hello("10.0.0.6", 10.0))
        self.assertFalse(w.hello("10.0.0.5", 30.0))
        self.assertEqual(w.host, "10.0.0.6")

    def test_one_that_was_silent_arrives_again(self):
        w = HelloWatch(60)
        w.hello("10.0.0.5", 0.0)
        w.hello("10.0.0.6", 10.0)
        w.hello("10.0.0.6", 70.0)
        self.assertTrue(w.hello("10.0.0.5", 80.0))
        self.assertEqual(w.host, "10.0.0.5")

    def test_the_silence_of_the_one_followed_is_noticed(self):
        w = HelloWatch(60)
        w.hello("10.0.0.5", 0.0)
        w.hello("10.0.0.6", 10.0)
        w.hello("10.0.0.5", 60.0)
        self.assertTrue(w.silence(71.0))
        self.assertIsNone(w.host)

    def test_with_nobody_followed_any_hello_connects(self):
        w = HelloWatch(60)
        w.hello("10.0.0.5", 0.0)
        w.hello("10.0.0.6", 10.0)
        w.hello("10.0.0.5", 60.0)
        w.silence(71.0)
        self.assertTrue(w.hello("10.0.0.5", 90.0))
        self.assertEqual(w.host, "10.0.0.5")
