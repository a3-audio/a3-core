"""Core asks REAPER for its whole state, at start-up and at the press of a key.

Core relays what REAPER reports, and REAPER reports its state once, when it
starts. A Core started after REAPER never heard it -- which is why the chain
had to start in one order, and why a Core restarted on its own came back not
knowing the channel strip (2026-09-25). REAPER's action "Control surface:
refresh all surfaces" (41743) makes it say everything again: measured on the
rig, some 27,000 packets in five seconds on the feedback port. So Core asks
for it once it is listening, whatever started first, and the window has a key
for it.
"""

import json
import sys
import unittest
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = (ROOT / "platform-config/debian-x86_64/a3-core"
           / "home/aaa/.local")
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_traffic import Traffic   # noqa: E402
from a3_core_web import start_window, stop_window, window_address   # noqa: E402

CORE = PACKAGE / "bin/a3-core.py"
PAGE = PACKAGE / "share/a3-core/web/index.html"
REAPER_OSC = PACKAGE / "share/a3-core/config/REAPER/OSC/a3-core.ReaperOSC"


class TheAction(unittest.TestCase):
    def test_it_is_refresh_all_surfaces(self):
        from a3_core_reaper import REFRESH_ACTION
        self.assertEqual("/action/41743", REFRESH_ACTION)

    def test_reaper_accepts_actions_at_all(self):
        """The pattern file decides what REAPER takes in. Without an ACTION
        pattern the request would be dropped on the floor -- worth holding
        while the file is being cut down to what A3 uses."""
        self.assertRegex(REAPER_OSC.read_text(),
                         r"(?m)^ACTION\b.*\bt/action/@")


class CoreAsksOnceItIsListening(unittest.TestCase):
    def test_start_up_asks_reaper_after_the_port_is_bound(self):
        source = CORE.read_text()
        bound = source.find("server = osc_server.BlockingOSCUDPServer(")
        asked = source.find("osc_reaper.send_message(REFRESH_ACTION")
        serving = source.rfind("server.serve_forever()")
        self.assertNotEqual(-1, asked, "a3-core.py never asks REAPER")
        self.assertLess(bound, asked, "asked before the answer has a port to land in")
        self.assertLess(asked, serving)


class TheWindowHasAKeyForIt(unittest.TestCase):
    def setUp(self):
        self.sent = []
        self.assertTrue(start_window(
            Traffic(), "127.0.0.1:0",
            send=lambda to, address, value: self.sent.append((to, address, value))))

    def tearDown(self):
        stop_window()

    def post(self, path):
        host, port = window_address()
        request = urllib.request.Request(f"http://{host}:{port}{path}",
                                         data=b"", method="POST")
        with urllib.request.urlopen(request, timeout=5) as answer:
            return answer.status, json.loads(answer.read())

    def test_the_key_sends_the_action_to_reaper(self):
        status, body = self.post("/api/refresh-reaper")
        self.assertEqual(200, status)
        self.assertEqual([("reaper", "/action/41743", 1.0)], self.sent)
        self.assertEqual("/action/41743", body["address"])

    def test_the_page_offers_the_key(self):
        page = PAGE.read_text()
        self.assertRegex(page, r'<button id="refreshreaper"')
        self.assertIn('"/api/refresh-reaper"', page)


class AWindowThatOnlyWatchesRefuses(unittest.TestCase):
    def setUp(self):
        self.assertTrue(start_window(Traffic(), "127.0.0.1:0"))

    def tearDown(self):
        stop_window()

    def test_without_a_sender_it_is_refused(self):
        host, port = window_address()
        request = urllib.request.Request(
            f"http://{host}:{port}/api/refresh-reaper", data=b"", method="POST")
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(request, timeout=5)
        self.assertEqual(400, caught.exception.code)


if __name__ == "__main__":
    unittest.main()
