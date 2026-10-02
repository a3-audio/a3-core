"""a3-jack waits for the Scarlett without a deadline, and comes back if it
falls over (2026-10-02).

After a reboot the Scarlett 18i20 -- switched on -- was not in /proc/asound
within systemd's 90 s start timeout. a3-jack's start-pre was killed, the unit
failed and was never tried again; JACK came up only when the maintainer
restarted a3-main by hand, and StemDeck, started without JACK, stayed down."""

import configparser
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UNIT = ROOT / ("platform-config/debian-x86_64/a3-core/home/aaa/.local/share/"
               "a3-core/config/systemd/user/a3-jack.service")


def service():
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read_string(UNIT.read_text())
    return parser["Service"]


class JackWaitsForTheCard(unittest.TestCase):
    def test_the_wait_for_the_card_has_no_deadline(self):
        self.assertIn("/proc/asound/USB", service()["ExecStartPre"])
        self.assertEqual(service().get("TimeoutStartSec"), "infinity")

    def test_a_failed_jack_is_started_again(self):
        self.assertEqual(service().get("Restart"), "on-failure")
        self.assertIn("RestartSec", service())


if __name__ == "__main__":
    unittest.main()
