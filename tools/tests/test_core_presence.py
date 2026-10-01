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
