"""Core replays the evening only once REAPER is listening (a3-core#56).

On a full chain start Core and REAPER come up in the same second, and REAPER
needs a few more to load its plug-ins before its OSC surface listens. Core's
replay of evening.json and its request for REAPER's state went out at once,
over UDP, to nobody -- REAPER kept its template's values until Core was
restarted on its own (2026-09-26).

Now Core asks REAPER to report (action 41743) until anything arrives on the
feedback port about a track the layout names -- the sign that the project,
not just the surface, is up -- and replays after that. The evening values are read before the
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


class FakeReaper:
    """A packet counter and a clock in one: each sleep() moves time on by one
    poll and lets REAPER send the next number of packets from a script."""

    def __init__(self, per_poll):
        from a3_core_reaper import Arrivals
        self.arrivals = Arrivals()
        self.script = list(per_poll)
        self.polls = 0

    def sleep(self, _seconds):
        self.polls += 1
        sent = self.script.pop(0) if self.script else self.last
        self.last = sent
        for _ in range(sent):
            self.arrivals.tick()


def wait_until_quiet(fake, **kwargs):
    from a3_core_reaper import wait_until_quiet as under_test
    done = []
    kwargs.setdefault("say", lambda _line: None)
    under_test(fake.arrivals, lambda: done.append(fake.polls),
               poll=0.25, quiet_rate=500, quiet_for=1.0, sleep=fake.sleep,
               **kwargs)
    return done


class WaitUntilQuiet(unittest.TestCase):
    """At 0.25 s a poll, 500/s is 125 packets a poll; a second is four polls.
    REAPER idles at about 14 a poll (its meters) and passes at about 575."""

    def test_a_reaper_already_quiet_needs_one_quiet_second(self):
        self.assertEqual([4], wait_until_quiet(FakeReaper([14] * 10)))

    def test_it_waits_out_a_pass(self):
        fake = FakeReaper([575] * 40 + [14] * 10)
        self.assertEqual([44], wait_until_quiet(fake))

    def test_a_dip_between_two_passes_is_not_the_end(self):
        """The cold start of 2026-09-26: two passes with a short dip."""
        fake = FakeReaper([575] * 40 + [20, 20] + [575] * 40 + [14] * 10)
        self.assertEqual([86], wait_until_quiet(fake))

    def test_it_gives_up_and_says_so_rather_than_wait_forever(self):
        said = []
        fake = FakeReaper([575] * 1000)
        done = wait_until_quiet(fake, give_up=10.0, say=said.append)
        self.assertEqual([40], done)
        self.assertTrue(any("gave up" in line for line in said), said)


class CoreIsWiredThatWay(unittest.TestCase):
    def setUp(self):
        self.source = CORE.read_text()

    def test_only_a_track_the_layout_names_counts_as_an_answer(self):
        """Not any packet: REAPER's surface talks about 1.8 s after start,
        while the template is still loading, and the load then put every
        replayed value back (device test, 2026-09-26). An empty REAPER has no
        tracks, so a report about one the layout names means the project is
        there."""
        handler = self.source.split("def reaper_feedback_handler", 1)[1]
        handler = handler.split("\ndef ", 1)[0]
        named = handler.find("if field is None:")
        heard = handler.find("reaper_heard.set()")
        self.assertNotEqual(-1, heard, "the feedback handler never notes REAPER")
        self.assertEqual(1, handler.count("reaper_heard.set()"))
        self.assertLess(named, heard)

    def test_a_stale_report_is_dropped_like_an_echo(self):
        handler = self.source.split("def reaper_feedback_handler", 1)[1]
        handler = handler.split("\ndef ", 1)[0]
        self.assertIn("echo_filter.is_stale(address, value)", handler)
        self.assertLess(handler.find("echo_filter.is_stale("),
                        handler.find("broadcast("))

    def test_the_evening_is_read_before_the_feedback_port_opens(self):
        read = self.source.find("evening = list(replayable(")
        opened = self.source.find("feedback_server = osc_server.BlockingOSCUDPServer(")
        self.assertNotEqual(-1, read)
        self.assertLess(read, opened)

    def test_every_packet_from_reaper_is_counted_before_anything_else(self):
        handler = self.source.split("def reaper_feedback_handler", 1)[1]
        handler = handler.split("\ndef ", 1)[0]
        counted = handler.find("reaper_arrivals.tick()")
        self.assertNotEqual(-1, counted)
        self.assertLess(counted, handler.find("return"))

    def test_the_replay_waits_for_the_pass_to_end_and_asks_nothing_after(self):
        """Measured 2026-09-26: a refresh right after a replay reports the
        old values of what was just set, and Core writes them down as news."""
        start = self.source.find("server = osc_server.BlockingOSCUDPServer(")
        tail = self.source[start:self.source.rfind("server.serve_forever()")]
        replay_path = tail.split("def replay_once_reaper_is_quiet", 1)[1]
        replay_path = replay_path.split("threading.Thread(", 1)[0]
        self.assertLess(replay_path.find("wait_until_quiet("),
                        replay_path.find("replay_evening("))
        self.assertNotIn("REFRESH_ACTION", replay_path)
        self.assertEqual(1, tail.count("REFRESH_ACTION"))

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
