"""What went past, counted -- and nothing more on the hot path.

Core routes OSC between three devices over UDP, and UDP never answers: a
sender shouting into a dead port looks exactly like one that arrives. This
module is what makes the difference visible. It is deliberately dull -- a
dict update and a bounded ring -- because it runs on every message, about a
hundred a second, and the interesting arithmetic belongs to whoever reads it.
"""

import json
import sys
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core"
                       / "home/aaa/.local/lib"))

from a3_core_traffic import (ANSWERERS, COMMANDERS, IN,   # noqa: E402
                            OUT, Traffic, peer_name)   # noqa: E402


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

    def test_cores_own_port_breaks_the_tie(self):
        """Motion on Core itself, which is how the rig runs since 2026-09-10.

        The window stopped naming Motion at all that day: `--motion
        127.0.0.1` met REAPER's default 127.0.0.1, and every incoming
        message became "127.0.0.1 (mehrdeutig)". The tie is breakable after
        all, just not by the *source* port -- by Core's own receiving port,
        which is fixed and carries an identity: REAPER only ever answers on
        the feedback port, the controllers only ever command on the main one.
        """
        motion_and_reaper = {"mixer": "192.168.8.11",
                             "motion": "127.0.0.1",
                             "reaper": "127.0.0.1"}
        self.assertEqual(
            peer_name("127.0.0.1", motion_and_reaper, only=COMMANDERS),
            "motion")
        self.assertEqual(
            peer_name("127.0.0.1", motion_and_reaper, only=ANSWERERS),
            "reaper")

    def test_a_narrowed_list_still_refuses_a_real_tie(self):
        """Narrowing must not become guessing.

        Both controllers on one box is the case no port can resolve -- they
        send to the same port. It has to stay ambiguous.
        """
        both_controllers = {"mixer": "127.0.0.1", "motion": "127.0.0.1",
                            "reaper": "192.168.8.20"}
        self.assertEqual(
            peer_name("127.0.0.1", both_controllers, only=COMMANDERS),
            "127.0.0.1 (mehrdeutig)")

    def test_a_device_that_cannot_be_on_this_port_is_not_named(self):
        """REAPER commanding on the main port does not happen.

        If something on REAPER's host ever does, it is not REAPER, and the
        raw host is the honest answer.
        """
        self.assertEqual(peer_name("127.0.0.1", RIG, only=COMMANDERS),
                         "127.0.0.1")


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
    only what is new. Rows stay whole regardless: there are only about forty
    understood addresses, and the reader needs their current totals, not a
    delta. The unknown table is not part of this cost at all -- it is not in
    `snapshot()` in the first place, cutoff or not."""

    def test_the_cutoff_splits_history_but_leaves_the_rows_whole(self):
        traffic = Traffic()
        traffic.seen(IN, "/a/0", 1, "motion")
        traffic.unknown("/track/3/name", 0.0, "reaper")
        cutoff = traffic.snapshot()["at"]
        traffic.seen(IN, "/a/0", 2, "motion")
        traffic.seen(IN, "/a/1", 3, "motion")

        since_cutoff = traffic.snapshot(history_since=cutoff)
        self.assertEqual([e["address"] for e in since_cutoff["history"]],
                         ["/a/0", "/a/1"])
        self.assertEqual(since_cutoff["rows"][0]["count"], 2)
        self.assertEqual(since_cutoff["unknown_addresses"], 1)
        self.assertNotIn("unhandled", since_cutoff)

        whole = traffic.snapshot()
        self.assertEqual(len(whole["history"]), 3)


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
                # Every row is built with a "count" key at creation, so
                # assertIn("count", row) could never go red -- it would pass
                # even against a torn read. A counter a torn read left at
                # zero is what would actually reveal the bug.
                self.assertTrue(
                    all(row["count"] > 0 for row in snapshot["rows"]))
        finally:
            stop.set()
            writer.join()


class UnderstoodAndUnknownAreTwoTables(unittest.TestCase):
    """The split this whole change exists for.

    REAPER dumps ~19,000 distinct addresses when its OSC surface connects, and
    Core can route about forty of them. Before this, every one of those became
    a permanent row, the snapshot grew to 6.4 MiB, and a stream ticking four
    times a second pushed it at 25.6 MiB/s until the maintainer's machine froze.

    a3-core.py has counted these since the wire branch and shown them
    nowhere -- its own comment says the list should be visible. `unknown()`
    and `unknown_snapshot()` are what make that possible: a table a caller
    can actually read, kept apart from the one `snapshot()` streams.
    """

    def setUp(self):
        self.traffic = Traffic()

    def test_an_unknown_address_gets_no_row(self):
        self.traffic.unknown("/track/3/name", 0.0, "reaper")
        self.assertEqual(self.traffic.snapshot()["rows"], [])

    def test_the_snapshot_reports_the_size_and_not_the_contents(self):
        for i in range(5):
            self.traffic.unknown(f"/track/{i}/name", float(i), "reaper")
        self.traffic.unknown("/track/0/name", 1.0, "reaper")
        snap = self.traffic.snapshot()
        self.assertEqual(snap["unknown_addresses"], 5)
        self.assertEqual(snap["unknown_messages"], 6)
        self.assertNotIn("unhandled", snap)

    def test_the_unknown_table_is_fetched_separately_and_looks_like_rows(self):
        self.traffic.unknown("/track/3/pan", 0.25, "reaper")
        row = self.traffic.unknown_snapshot()["rows"][0]
        for key in ("address", "count", "last_value", "last_type",
                    "last_seen", "peer"):
            self.assertIn(key, row)
        self.assertEqual(row["address"], "/track/3/pan")
        self.assertEqual(row["last_value"], 0.25)
        self.assertEqual(row["last_type"], "float")

    def test_understood_and_unknown_do_not_mix(self):
        self.traffic.seen(IN, "/channel/0/gain", 0.5, "motion")
        self.traffic.unknown("/track/3/name", 0.0, "reaper")
        self.assertEqual([r["address"] for r in self.traffic.snapshot()["rows"]],
                         ["/channel/0/gain"])
        self.assertEqual([r["address"]
                          for r in self.traffic.unknown_snapshot()["rows"]],
                         ["/track/3/name"])


class TheCapsEvictAndSaySo(unittest.TestCase):
    """A table that silently loses rows would be the kind of lie this branch
    has already taken eleven findings for."""

    def test_the_unknown_table_stops_at_its_cap(self):
        traffic = Traffic(unknown_cap=10)
        for i in range(25):
            traffic.unknown(f"/track/{i}/name", 0.0, "reaper")
        snap = traffic.unknown_snapshot()
        self.assertEqual(len(snap["rows"]), 10)
        self.assertEqual(snap["evicted"], 15)

    def test_the_row_table_stops_at_its_cap(self):
        traffic = Traffic(row_cap=10)
        for i in range(25):
            traffic.seen(IN, f"/channel/{i}/gain", 0.0, "motion")
        snap = traffic.snapshot()
        self.assertEqual(len(snap["rows"]), 10)
        self.assertEqual(snap["evicted"]["rows"], 15)

    def test_eviction_drops_the_least_recently_seen(self):
        traffic = Traffic(unknown_cap=3)
        for name in ("a", "b", "c"):
            traffic.unknown(f"/track/{name}", 0.0, "reaper")
        traffic.unknown("/track/a", 1.0, "reaper")     # a is touched again
        traffic.unknown("/track/d", 0.0, "reaper")     # pushes one out
        left = {r["address"] for r in traffic.unknown_snapshot()["rows"]}
        self.assertEqual(left, {"/track/a", "/track/c", "/track/d"})

    def test_a_repeat_does_not_count_against_the_cap(self):
        traffic = Traffic(unknown_cap=3)
        for _ in range(50):
            traffic.unknown("/track/3/name", 0.0, "reaper")
        snap = traffic.unknown_snapshot()
        self.assertEqual(len(snap["rows"]), 1)
        self.assertEqual(snap["rows"][0]["count"], 50)
        self.assertEqual(snap["evicted"], 0)

    def test_nothing_is_evicted_below_the_cap(self):
        traffic = Traffic(unknown_cap=10)
        for i in range(10):
            traffic.unknown(f"/track/{i}", 0.0, "reaper")
        self.assertEqual(traffic.unknown_snapshot()["evicted"], 0)
        self.assertEqual(traffic.snapshot()["evicted"]["unknown"], 0)

    def test_unknown_messages_counts_messages_ever_seen_not_rows_still_held(
            self):
        """"N Nachrichten" on the page reads like a lifetime total. An
        evicted row's messages already happened -- eviction dropping the row
        must not un-happen them, so the total must not shrink when it does."""
        traffic = Traffic(unknown_cap=2)
        traffic.unknown("/track/a", 0.0, "reaper")
        traffic.unknown("/track/b", 0.0, "reaper")
        traffic.unknown("/track/c", 0.0, "reaper")   # evicts /track/a
        self.assertEqual(traffic.unknown_snapshot()["evicted"], 1)
        self.assertEqual(traffic.snapshot()["unknown_messages"], 3)


class TheRealShapeOfTheIncident(unittest.TestCase):
    """The numbers from the box, as a test.

    19,335 unknown addresses against 44 understood ones. The point is not the
    cap -- 19,335 is below it -- but that the snapshot the stream carries stays
    small anyway, because the unknown table is not in it.
    """

    def test_nineteen_thousand_unknowns_do_not_enter_the_snapshot(self):
        traffic = Traffic()
        for i in range(19335):
            traffic.unknown(f"/track/{i // 8}/param/{i % 8}", 0.0, "reaper")
        for i in range(44):
            traffic.seen(IN, f"/channel/{i}/gain", 0.0, "motion")

        snap = traffic.snapshot()
        self.assertEqual(len(snap["rows"]), 44)
        self.assertEqual(snap["unknown_addresses"], 19335)
        self.assertEqual(len(json.dumps(snap)) < 64 * 1024, True)
        self.assertEqual(len(traffic.unknown_snapshot()["rows"]), 19335)


if __name__ == "__main__":
    unittest.main()


class AnAddressDoesNotStayUnknownOnceItIsUnderstood(unittest.TestCase):
    """The unknown table is restored across a restart, and what Core can route
    changes between restarts -- the reverse table grew on 2026-09-12 and
    twenty-three addresses moved from unrecognised to routed. Their old rows
    stayed, so the window showed them in both tables at once, and somebody
    hunting a dead wire would have found `/track/1/fx/1/fxparam/1/value`
    listed as not understood while Core was routing it perfectly.

    An instrument that reads wrong about itself is worse than no instrument.
    """

    def test_a_row_seen_incoming_clears_its_unknown_row(self):
        traffic = Traffic()
        traffic.unknown("/track/1/fx/1/fxparam/1/value", 0.5, "reaper")
        self.assertEqual(len(traffic.unknown_snapshot()["rows"]), 1)

        traffic.seen(IN, "/track/1/fx/1/fxparam/1/value", 0.5, "reaper")
        self.assertEqual(traffic.unknown_snapshot()["rows"], [])

    def test_an_outgoing_row_does_not(self):
        """Core sends `/track/n/fx/1/fxparam/1/value` and cannot read the
        crossfade back out of it. Known outbound, unknown inbound -- and that
        pair is the truth, not a leftover."""
        traffic = Traffic()
        traffic.unknown("/track/10/fx/1/fxparam/1/value", 0.5, "reaper")
        traffic.seen(OUT, "/track/10/fx/1/fxparam/1/value", 0.5, "reaper")
        self.assertEqual(len(traffic.unknown_snapshot()["rows"]), 1)

    def test_it_costs_nothing_on_the_hot_path(self):
        """Only the branch that *creates* a row looks at the unknown table.
        seen() runs about a hundred times a second and a repeat message must
        not pay for a lookup it can never need."""
        traffic = Traffic()
        traffic.seen(IN, "/channel/0/gain", 0.5, "mixer")
        traffic.unknown("/channel/0/gain", 0.5, "reaper")

        # The row already exists, so this takes the repeat path and leaves
        # the unknown row alone. Saying so out loud because it looks like a
        # gap: it is the price of keeping the check off the hot path, and
        # an address cannot go from routed back to unrecognised anyway.
        traffic.seen(IN, "/channel/0/gain", 0.6, "mixer")
        self.assertEqual(len(traffic.unknown_snapshot()["rows"]), 1)
