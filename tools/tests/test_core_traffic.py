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


class ChainedDeltasLoseAndDuplicateNothing(unittest.TestCase):
    """The delta stream's whole invariant: an entry belongs to a snapshot's
    copy if and only if its own `at` is not greater than that snapshot's
    `at`. `seen()` and `snapshot()` both stamp `at` as the first thing
    inside the shared lock so that the order of `at` values matches the
    order of lock acquisitions -- without that, a snapshot can stamp `at`
    before an entry that was appended a moment later, and that entry then
    reads as "already sent" to every future cutoff without ever having been
    sent at all."""

    def test_the_union_of_every_delta_is_everything_written_once_each(self):
        writers = 8
        writes_each = 200
        total = writers * writes_each
        # Comfortably above `total` so the ring never evicts anything here --
        # this test is about the delta race, not about ring size.
        traffic = Traffic(history=total + 100)

        counter_lock = threading.Lock()
        counter = [0]

        def next_id():
            with counter_lock:
                counter[0] += 1
                return counter[0]

        def hammer():
            for _ in range(writes_each):
                traffic.seen(IN, "/a/0", next_id(), "motion")

        writer_threads = [threading.Thread(target=hammer)
                          for _ in range(writers)]

        collected = []
        stop = threading.Event()

        def consume():
            since = None
            while not stop.is_set():
                snap = traffic.snapshot(history_since=since)
                collected.extend(entry["value"] for entry in snap["history"])
                since = snap["at"]
            # One more pass after the writers are confirmed joined, to pick
            # up whatever landed between the last loop iteration and stop().
            snap = traffic.snapshot(history_since=since)
            collected.extend(entry["value"] for entry in snap["history"])

        consumer = threading.Thread(target=consume)
        consumer.start()
        for thread in writer_threads:
            thread.start()
        for thread in writer_threads:
            thread.join()
        stop.set()
        consumer.join()

        # Sorted equality catches both a gap (a value missing) and a
        # duplicate (a value appearing twice, which would make the sorted
        # list longer than range(1, total + 1) even if every value is
        # present).
        self.assertEqual(sorted(collected), list(range(1, total + 1)))


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


class HistorySinceMakesTheStreamACheapDelta(unittest.TestCase):
    """A stream ticking four times a second must not resend the whole ring
    every tick -- that is a bigger flood than the one this feature replaces.
    `history_since` is how a caller who already has an earlier `at` asks for
    only what is new. Rows and unhandled stay whole regardless: there are
    only a few hundred addresses, and the reader needs their current totals,
    not a delta."""

    def test_the_cutoff_splits_history_but_leaves_rows_and_unhandled_whole(
            self):
        traffic = Traffic()
        traffic.seen(IN, "/a/0", 1, "motion")
        traffic.unhandled("/track/3/name")
        cutoff = traffic.snapshot()["at"]
        traffic.seen(IN, "/a/0", 2, "motion")
        traffic.seen(IN, "/a/1", 3, "motion")

        since_cutoff = traffic.snapshot(history_since=cutoff)
        self.assertEqual([e["address"] for e in since_cutoff["history"]],
                         ["/a/0", "/a/1"])
        self.assertEqual(since_cutoff["rows"][0]["count"], 2)
        self.assertEqual(since_cutoff["unhandled"], {"/track/3/name": 1})

        whole = traffic.snapshot()
        self.assertEqual(len(whole["history"]), 3)


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
