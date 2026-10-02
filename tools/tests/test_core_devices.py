"""Every device says which truth it speaks, and Core says whether it is its own.

The mixer runs on a machine of its own and reads a copy of a3-osc.json put
beside its script at deploy. A copy is a second truth the day it goes stale,
and OSC over UDP would never say so: the desk would send the old words to an
address nobody listens on. So the desk names itself with the hash of its copy
(`/device/hello`), and Core's window shows whether that is the file Core runs
on (decided 2026-09-30).
"""

import hashlib
import json
import sys
import tempfile
import unittest
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_core_devices  # noqa: E402
import a3_osc           # noqa: E402

TRUTH = PACKAGE / "usr/share/a3/a3-osc.json"


class TheHashIsTheFilesBytes(unittest.TestCase):
    def test_it_is_the_sha256_of_the_file(self):
        self.assertEqual(a3_core_devices.truth_hash(TRUTH),
                         hashlib.sha256(TRUTH.read_bytes()).hexdigest())

    def test_one_changed_byte_is_another_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / "a3-osc.json"
            copy.write_bytes(TRUTH.read_bytes() + b" ")
            self.assertNotEqual(a3_core_devices.truth_hash(copy),
                                a3_core_devices.truth_hash(TRUTH))


class CoreComparesWhatItHears(unittest.TestCase):
    def setUp(self):
        self.devices = a3_core_devices.Devices(own_hash="abc")

    def test_a_device_on_the_same_truth_matches(self):
        self.devices.heard("mixer", "abc", now=100.0)
        (row,) = self.devices.snapshot(now=103.0)
        self.assertEqual(row, {"device": "mixer", "hash": "abc",
                               "matches": True, "age": 3.0})

    def test_a_device_on_another_truth_does_not(self):
        self.devices.heard("mixer", "old", now=100.0)
        (row,) = self.devices.snapshot(now=100.0)
        self.assertFalse(row["matches"])

    def test_the_last_word_counts(self):
        self.devices.heard("mixer", "old", now=100.0)
        self.devices.heard("mixer", "abc", now=200.0)
        (row,) = self.devices.snapshot(now=200.0)
        self.assertTrue(row["matches"])

    def test_it_says_when_a_device_changed_its_mind(self):
        self.assertTrue(self.devices.heard("mixer", "old", now=1.0),
                        "the first word is news")
        self.assertFalse(self.devices.heard("mixer", "old", now=2.0),
                         "the same word again is not")
        self.assertTrue(self.devices.heard("mixer", "abc", now=3.0))


class TheWindowShowsIt(unittest.TestCase):
    def setUp(self):
        from a3_core_traffic import Traffic
        from a3_core_web import start_window
        self.devices = a3_core_devices.Devices(own_hash="abc")
        self.devices.heard("mixer", "old", now=0.0)
        self.assertTrue(start_window(Traffic(), "127.0.0.1:0",
                                     devices=self.devices))

    def tearDown(self):
        from a3_core_web import stop_window
        stop_window()

    def get(self, path):
        from a3_core_web import window_address
        host, port = window_address()
        with urllib.request.urlopen(f"http://{host}:{port}{path}", timeout=5) as r:
            return r.read().decode()

    def test_the_api_says_whose_truth_differs(self):
        payload = json.loads(self.get("/api/devices"))
        self.assertEqual(payload["own"], "abc")
        (row,) = payload["rows"]
        self.assertEqual((row["device"], row["matches"]), ("mixer", False))

    def test_the_page_asks_for_it(self):
        self.assertIn("/api/devices", self.get("/"))


class CoreListensForIt(unittest.TestCase):
    """Read off the source, like test_core_handlers_are_mapped: Core itself
    cannot be started in a test."""

    def setUp(self):
        self.source = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()

    def test_the_word_is_mapped(self):
        self.assertIn('_truth.address("device.hello")', self.source)

    def test_what_it_hears_is_kept_and_served(self):
        self.assertIn("_devices.heard(", self.source)
        self.assertIn("devices=_devices", self.source)


class TheWordIsInTheTruth(unittest.TestCase):
    def test_device_hello_is_an_address_of_ours(self):
        truth = a3_osc.load(TRUTH)
        self.assertEqual(truth.pattern("device.hello"), "/device/hello")
        self.assertIn("core", truth.addresses()["device.hello"]["to"])
        self.assertIn("mixer", truth.addresses()["device.hello"]["from"])




class CoreComparesTheFingerprint(unittest.TestCase):
    """Step 2 of truth-from-core: the desk hashes the body it fetched from
    /api/truth, which is Core's canonical truth -- so Core's own hash for the
    hello is the fingerprint, not the package file's bytes."""

    def test_cores_hello_compares_the_fingerprint(self):
        core = (Path(__file__).resolve().parents[2] / "platform-config/debian-x86_64/a3-core"
                / "home/aaa/.local/bin/a3-core.py").read_text()
        self.assertIn("Devices(_truth.fingerprint())", core)


if __name__ == "__main__":
    unittest.main()
