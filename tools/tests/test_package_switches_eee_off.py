"""Energy Efficient Ethernet stays off on every wired port of the Core.

2026-10-17: EEE was "enabled - active" on the audio port (igb) of both
machines; one of them lost its link every one or two minutes (148 carrier
changes by midday), each drop a hole of about three seconds in the AES67
stream. With EEE off by hand: no drop in ten minutes. The LAN port (e1000e)
has a history of drops, too.

systemd 258 added [EnergyEfficientEthernet] Enable= to .link files. The
package ships it as a drop-in to the stock 99-default.link, which every
interface falls through to: it covers whichever ports the installer was told
about (they are not known when the package is built), and it keeps the stock
naming policy, because a link file of our own would win the match and take
that policy with it.
"""

import configparser
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64" / "a3-core"
DROPIN = PACKAGE / "etc" / "systemd" / "network" / "99-default.link.d" / "a3-eee.conf"
CONFFILES = PACKAGE / "DEBIAN" / "conffiles"


def parsed():
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read_string(DROPIN.read_text())
    return parser


class PackageSwitchesEeeOff(unittest.TestCase):
    def test_package_ships_the_dropin(self):
        self.assertTrue(DROPIN.exists(), f"{DROPIN} is not shipped")

    def test_eee_is_switched_off(self):
        section = parsed()["EnergyEfficientEthernet"]
        self.assertEqual("false", section["Enable"].lower())

    def test_dropin_adds_no_match_that_narrows_the_default(self):
        """A [Match] here would be ANDed with the stock one and could leave a
        port out."""
        self.assertNotIn("Match", parsed().sections())

    def test_dropin_is_a_conffile(self):
        self.assertIn("/etc/systemd/network/99-default.link.d/a3-eee.conf",
                      CONFFILES.read_text().split())


if __name__ == "__main__":
    unittest.main()
