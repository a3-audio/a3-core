"""On a Core, StemDeck goes and comes with a3-jack.

StemDeck's own unit names no unit of the Core since 2026-10-05: it may run on
a machine of its own, and a unit bound to a missing unit never starts. The
binding it had (after a late sound card on 2026-10-02 StemDeck stayed down
when JACK came up later) is the Core's to add, as a drop-in shipped with the
user config: on a Core, a3-jack exists."""

import configparser
import unittest
from pathlib import Path

DROP_IN = (Path(__file__).resolve().parents[2]
           / "platform-config/debian-x86_64/a3-core/home/aaa/.local/share/a3-core"
             "/config/systemd/user/stemdeck.service.d/a3-core.conf")


def unit():
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read_string(DROP_IN.read_text())
    return parser["Unit"]


class StemDeckIsBoundToJackOnTheCore(unittest.TestCase):
    def test_it_is_bound_to_jack(self):
        self.assertIn("a3-jack.service", unit().get("BindsTo", "").split())

    def test_it_starts_after_jack(self):
        self.assertIn("a3-jack.service", unit().get("After", "").split())


if __name__ == "__main__":
    unittest.main()
