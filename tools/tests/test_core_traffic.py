"""What went past, counted -- and nothing more on the hot path.

Core routes OSC between three devices over UDP, and UDP never answers: a
sender shouting into a dead port looks exactly like one that arrives. This
module is what makes the difference visible. It is deliberately dull -- a
dict update and a bounded ring -- because it runs on every message, about a
hundred a second, and the interesting arithmetic belongs to whoever reads it.
"""

import sys
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core"
                       / "home/aaa/.local/lib"))

from a3_core_traffic import IN, OUT, Traffic, peer_name   # noqa: E402


RIG = {"mixer": "192.168.43.55",
       "motion": "192.168.43.54",
       "reaper": "127.0.0.1"}


class NamingWhoItWas(unittest.TestCase):
    def test_a_host_on_the_rig_gets_its_device_name(self):
        self.assertEqual(peer_name("192.168.43.55", RIG), "mixer")
        self.assertEqual(peer_name("192.168.43.54", RIG), "motion")

    def test_a_stranger_is_shown_as_itself(self):
        self.assertEqual(peer_name("10.0.0.9", RIG), "10.0.0.9")

    def test_two_devices_on_one_host_are_not_guessed_at(self):
        """The dev box, where everything is 127.0.0.1.

        The source port cannot break the tie: SimpleUDPClient never binds
        one, so the OS hands out a fresh ephemeral port per sender. A wrong
        label is worse than no label.
        """
        one_box = {"mixer": "127.0.0.1", "motion": "127.0.0.1",
                   "reaper": "127.0.0.1"}
        self.assertEqual(peer_name("127.0.0.1", one_box),
                         "127.0.0.1 (mehrdeutig)")


class CountingByAddressAndDirection(unittest.TestCase):
    def setUp(self):
        self.traffic = Traffic()

    def rows(self):
        return {(r["direction"], r["address"]): r
                for r in self.traffic.snapshot()["rows"]}

    def test_the_same_address_in_both_directions_is_two_rows(self):
        self.traffic.seen(IN, "/channel/0/gain", 0.5, "motion")
        self.traffic.seen(OUT, "/channel/0/gain", 0.5, "mixer")
        rows = self.rows()
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[(IN, "/channel/0/gain")]["count"], 1)
        self.assertEqual(rows[(OUT, "/channel/0/gain")]["count"], 1)

    def test_repeats_add_up_and_the_last_value_wins(self):
        for value in (0.1, 0.2, 0.3):
            self.traffic.seen(IN, "/channel/0/gain", value, "motion")
        row = self.rows()[(IN, "/channel/0/gain")]
        self.assertEqual(row["count"], 3)
        self.assertEqual(row["last_value"], 0.3)

    def test_an_address_never_seen_has_no_row(self):
        """The shape of the bug this whole window exists for.

        The mixer's fifteen addresses went to the beat-analyzer for two days.
        What made it invisible was that nothing anywhere said "zero".
        """
        self.traffic.seen(IN, "/channel/0/azimuth", 1.0, "motion")
        self.assertNotIn((IN, "/channel/0/gain"), self.rows())


class KeepingTheType(unittest.TestCase):
    """A string "1" and a float 1.0 mean different things in this rig.

    They are the difference between the A3 Mixer's momentary edge and A3
    Motion's state -- see a3_core_buttons. A monitor that showed both as 1
    would hide exactly what one goes looking for.
    """

    def setUp(self):
        self.traffic = Traffic()

    def test_a_string_is_reported_as_a_string(self):
        self.traffic.seen(IN, "/channel/0/pfl", "1", "mixer")
        row = self.traffic.snapshot()["rows"][0]
        self.assertEqual(row["last_value"], "1")
        self.assertEqual(row["last_type"], "str")

    def test_a_float_is_reported_as_a_float(self):
        self.traffic.seen(IN, "/channel/0/pfl", 1.0, "motion")
        row = self.traffic.snapshot()["rows"][0]
        self.assertEqual(row["last_value"], 1.0)
        self.assertEqual(row["last_type"], "float")


class TheRingHoldsItsBound(unittest.TestCase):
    def test_the_oldest_falls_out_and_the_newest_is_kept(self):
        traffic = Traffic(history=3)
        for i in range(5):
            traffic.seen(IN, f"/a/{i}", i, "motion")
        history = traffic.snapshot()["history"]
        self.assertEqual(len(history), 3)
        self.assertEqual([entry["address"] for entry in history],
                         ["/a/2", "/a/3", "/a/4"])

    def test_the_counters_are_not_bounded_by_the_ring(self):
        """The ring forgets; the counters do not. Different jobs."""
        traffic = Traffic(history=3)
        for _ in range(50):
            traffic.seen(IN, "/a/0", 1, "motion")
        self.assertEqual(traffic.snapshot()["rows"][0]["count"], 50)


class TheReaderComputesTheRate(unittest.TestCase):
    def test_two_snapshots_carry_what_a_rate_needs(self):
        traffic = Traffic()
        first = traffic.snapshot()
        for _ in range(10):
            traffic.seen(IN, "/a/0", 1, "motion")
        second = traffic.snapshot()

        self.assertGreater(second["at"], first["at"])
        self.assertEqual(second["rows"][0]["count"], 10)
        self.assertEqual(first["rows"], [])


class TheUnhandledAreVisible(unittest.TestCase):
    """a3-core.py has counted these since the wire branch and shown them
    nowhere. Its own comment says the list should be visible."""

    def test_they_are_counted_by_address(self):
        traffic = Traffic()
        traffic.unhandled("/track/3/name")
        traffic.unhandled("/track/3/name")
        traffic.unhandled("/track/7/pan")
        self.assertEqual(traffic.snapshot()["unhandled"],
                         {"/track/3/name": 2, "/track/7/pan": 1})


class ManyThreadsAtOnce(unittest.TestCase):
    def test_nothing_is_lost_when_four_senders_write_together(self):
        """Core has at least two OSC threads -- the main port and REAPER's
        feedback port -- and the HTTP thread reads while they write."""
        traffic = Traffic(history=10)

        def hammer():
            for _ in range(500):
                traffic.seen(IN, "/a/0", 1, "motion")

        threads = [threading.Thread(target=hammer) for _ in range(4)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()

        self.assertEqual(traffic.snapshot()["rows"][0]["count"], 2000)

    def test_a_reader_during_a_write_storm_gets_a_whole_snapshot(self):
        traffic = Traffic(history=10)
        stop = threading.Event()

        def hammer():
            while not stop.is_set():
                traffic.seen(IN, "/a/0", 1, "motion")

        writer = threading.Thread(target=hammer)
        writer.start()
        try:
            for _ in range(50):
                snapshot = traffic.snapshot()
                self.assertLessEqual(len(snapshot["history"]), 10)
                for row in snapshot["rows"]:
                    self.assertIn("count", row)
        finally:
            stop.set()
            writer.join()


if __name__ == "__main__":
    unittest.main()
