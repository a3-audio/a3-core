"""Every device says which truth it speaks, and Core says whether it is its own.

The mixer runs on a machine of its own and reads a copy of a3-osc.json put
beside its script at deploy. A copy is a second truth the day it goes stale,
and OSC over UDP would never say so: the desk would send the old words to an
address nobody listens on. So the desk names itself with the hash of its copy
(`/device/hello`), and Core's window shows whether that is the file Core runs
on (decided 2026-09-30).
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(ROOT / "tools/lib"))

import a3_osc           # noqa: E402

TRUTH = PACKAGE / "usr/share/a3/a3-osc.json"


class TheWordIsInTheTruth(unittest.TestCase):
    def test_device_hello_is_an_address_of_ours(self):
        truth = a3_osc.load(TRUTH)
        self.assertEqual(truth.pattern("device.hello"), "/device/hello")
        self.assertIn("core", truth.addresses()["device.hello"]["to"])
        self.assertIn("mixer", truth.addresses()["device.hello"]["from"])


if __name__ == "__main__":
    unittest.main()
