"""Core replays the evening only once REAPER is listening (a3-core#56).

On a full chain start Core and REAPER come up in the same second, and REAPER
needs a few more to load its plug-ins before its OSC surface listens. Core's
replay of evening.json and its request for REAPER's state went out at once,
over UDP, to nobody -- REAPER kept its template's values until Core was
restarted on its own (2026-09-26).

Now Core asks REAPER to report (action 41743) until anything arrives on the
feedback port, and replays after that. The evening values are read before the
feedback port opens: every value REAPER reports is also written to
evening.json, so a template announcing itself first would otherwise overwrite
the very file the replay is about to read.
"""

import sys
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

CORE = PACKAGE / "bin/a3-core.py"


def when_reaper_listens(*args, **kwargs):
    from a3_core_reaper import when_reaper_listens as under_test
    return under_test(*args, **kwargs)


class WhenReaperListens(unittest.TestCase):
    def setUp(self):
        self.heard = threading.Event()
        self.log = []

    def ask(self):
        self.log.append("ask")

    def then(self):
        self.log.append("then")

    def run_in_background(self, every=0.01):
        worker = threading.Thread(
            target=when_reaper_listens, daemon=True,
            args=(self.heard, self.ask, self.then),
            kwargs={"every": every, "say": lambda _line: None})
        worker.start()
        return worker

    def test_a_reaper_already_up_is_asked_once_and_the_replay_follows(self):
        self.heard.set()
        self.run_in_background().join(1.0)
        self.assertEqual(["ask", "then"], self.log)

    def test_a_silent_reaper_is_asked_again_and_nothing_is_replayed(self):
        worker = self.run_in_background()
        time.sleep(0.1)
        self.assertNotIn("then", self.log)
        self.assertGreater(self.log.count("ask"), 2)
        self.heard.set()
        worker.join(1.0)

    def test_once_it_answers_the_replay_runs_once_and_the_asking_stops(self):
        worker = self.run_in_background()
        time.sleep(0.05)
        self.heard.set()
        worker.join(1.0)
        self.assertFalse(worker.is_alive())
        self.assertEqual("then", self.log[-1])
        self.assertEqual(1, self.log.count("then"))

    def test_it_says_once_that_it_is_waiting(self):
        said = []
        worker = threading.Thread(
            target=when_reaper_listens, daemon=True,
            args=(self.heard, self.ask, self.then),
            kwargs={"every": 0.01, "say": said.append})
        worker.start()
        time.sleep(0.1)
        self.heard.set()
        worker.join(1.0)
        waiting = [line for line in said if "does not answer" in line]
        self.assertEqual(1, len(waiting), said)


class CoreIsWiredThatWay(unittest.TestCase):
    def setUp(self):
        self.source = CORE.read_text()

    def test_every_packet_from_reaper_counts_as_an_answer(self):
        handler = self.source.split("def reaper_feedback_handler", 1)[1]
        first_return = handler.find("return")
        heard = handler.find("reaper_heard.set()")
        self.assertNotEqual(-1, heard, "the feedback handler never notes REAPER")
        self.assertLess(heard, first_return)

    def test_the_evening_is_read_before_the_feedback_port_opens(self):
        read = self.source.find("evening = list(replayable(")
        opened = self.source.find("feedback_server = osc_server.BlockingOSCUDPServer(")
        self.assertNotEqual(-1, read)
        self.assertLess(read, opened)

    def test_the_replay_waits_in_a_thread_of_its_own(self):
        start = self.source.find("server = osc_server.BlockingOSCUDPServer(")
        tail = self.source[start:]
        self.assertIn("target=when_reaper_listens", tail)
        self.assertLess(tail.find("target=when_reaper_listens"),
                        tail.rfind("server.serve_forever()"))
        # No second, unconditional replay or refresh left beside it.
        self.assertNotIn("\n    replay_evening(", tail)
        self.assertNotIn("\n    osc_reaper.send_message(REFRESH_ACTION", tail)


if __name__ == "__main__":
    unittest.main()
