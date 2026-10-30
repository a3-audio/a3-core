"""On a Core, StemDeck goes and comes with the audio server, PipeWire.

StemDeck's own unit names no unit of the Core since 2026-10-05: it may run on
a machine of its own, and a unit bound to a missing unit never starts. The
binding it had (after a late sound card on 2026-10-02 StemDeck stayed down
when the audio server came up later) is the Core's to add, as a drop-in
shipped with the user config. The server was a3-jack until 2026-10-13; it is
PipeWire since, and a unit still bound to a3-jack would start jackd and take
the sound card away from PipeWire."""

import configparser
import unittest

from tools.tests.paths import DEPLOYMENT_UNITS

DROP_IN = DEPLOYMENT_UNITS / "stemdeck.service.d/a3-core.conf"


def unit():
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read_string(DROP_IN.read_text())
    return parser["Unit"]


class StemDeckIsBoundToPipeWireOnTheCore(unittest.TestCase):
    def test_it_is_bound_to_pipewire(self):
        self.assertIn("pipewire.service", unit().get("BindsTo", "").split())

    def test_it_starts_after_pipewire_and_its_session_manager(self):
        after = unit().get("After", "").split()
        self.assertIn("pipewire.service", after)
        self.assertIn("wireplumber.service", after)

    def test_it_names_no_jack_server(self):
        unit_lines = [line for line in DROP_IN.read_text().splitlines()
                      if line.strip() and not line.lstrip().startswith("#")]
        self.assertFalse([line for line in unit_lines if "a3-jack" in line])


if __name__ == "__main__":
    unittest.main()
