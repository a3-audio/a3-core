"""The address list, kept across a restart.

Core was restarted six times on 2026-09-12 and the window forgot everything
each time: 19,335 unknown addresses in the morning, five by 13:20. REAPER
counts its vocabulary out once, when its OSC surface connects, and never
again -- so a Core that came up afterwards saw an empty table and no way to
ask.

What is kept is the **list**, not the history. The ring is two minutes of what
just happened and was never an archive; carrying it across a restart would be
carrying a different thing entirely.

The two rules this lives under, both measured here rather than asserted:
`seen()` never touches a file, and a burst of new addresses is one write.
"""

import json
import os
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = (ROOT / "platform-config/debian-x86_64/a3-core"
           / "home/aaa/.local")
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_seen import SeenFile, state_path   # noqa: E402
from a3_core_traffic import IN, OUT, Traffic    # noqa: E402


class WhereItLives(unittest.TestCase):
    def test_it_follows_xdg_state_home(self):
        before = os.environ.get("XDG_STATE_HOME")
        os.environ["XDG_STATE_HOME"] = "/tmp/somewhere"
        try:
            self.assertEqual(state_path(),
                             Path("/tmp/somewhere/a3-core/seen.json"))
        finally:
            if before is None:
                del os.environ["XDG_STATE_HOME"]
            else:
                os.environ["XDG_STATE_HOME"] = before

    def test_without_it_the_default_is_under_home(self):
        before = os.environ.pop("XDG_STATE_HOME", None)
        try:
            self.assertEqual(state_path(),
                             Path.home() / ".local/state/a3-core/seen.json")
        finally:
            if before is not None:
                os.environ["XDG_STATE_HOME"] = before


class ItComesBack(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.path = Path(self.folder.name) / "seen.json"

    def tearDown(self):
        self.folder.cleanup()

    def _saved(self):
        traffic = Traffic()
        traffic.seen(IN, "/channel/0/gain", 0.5, "mixer")
        traffic.seen(IN, "/channel/0/gain", 0.6, "mixer")
        traffic.seen(OUT, "/track/12/volume", 0.4, "reaper")
        traffic.unknown("/track/3/name", "kick", "reaper")
        SeenFile(self.path, traffic).write()
        return traffic

    def test_the_understood_addresses_come_back(self):
        self._saved()
        after = Traffic()
        SeenFile(self.path, after).restore()
        rows = {(row["direction"], row["address"]): row
                for row in after.snapshot()["rows"]}
        self.assertEqual(rows[(IN, "/channel/0/gain")]["count"], 2)
        self.assertEqual(rows[(IN, "/channel/0/gain")]["peer"], "mixer")
        self.assertIn((OUT, "/track/12/volume"), rows)

    def test_the_unknown_addresses_come_back(self):
        self._saved()
        after = Traffic()
        SeenFile(self.path, after).restore()
        rows = after.unknown_snapshot()["rows"]
        self.assertEqual([row["address"] for row in rows], ["/track/3/name"])
        self.assertEqual(after.snapshot()["unknown_addresses"], 1)

    def test_the_history_does_not_come_back(self):
        """Two minutes of what just happened, and none of it happened in this
        process. A ring restored from a dead Core would be a lie about now."""
        self._saved()
        after = Traffic()
        SeenFile(self.path, after).restore()
        self.assertEqual(after.snapshot()["history"], [])

    def test_the_last_value_does_not_come_back(self):
        """It was true before the restart and says nothing about now. The
        recall is what answers "what is it at the moment" -- see
        a3_core_recall -- and a stale number sitting in the value column
        would be indistinguishable from a live one."""
        self._saved()
        after = Traffic()
        SeenFile(self.path, after).restore()
        row = after.snapshot()["rows"][0]
        self.assertIsNone(row["last_value"])
        self.assertEqual(row["last_type"], "")

    def test_an_old_address_reads_as_old(self):
        """The age column is the one thing a reader uses to tell a live wire
        from a dead one, so a restored row must not read as "gerade"."""
        traffic = Traffic()
        traffic.seen(IN, "/channel/0/gain", 0.5, "mixer")
        file = SeenFile(self.path, traffic)
        file.write()

        # Rewrite the file as though it had been saved an hour ago.
        saved = json.loads(self.path.read_text())
        saved["at"] -= 3600.0
        saved["rows"] = [[d, a, c, p, at - 3600.0]
                         for d, a, c, p, at in saved["rows"]]
        self.path.write_text(json.dumps(saved))

        after = Traffic()
        SeenFile(self.path, after).restore()
        snapshot = after.snapshot()
        age = snapshot["at"] - snapshot["rows"][0]["last_seen"]
        self.assertGreater(age, 3500)

    def test_a_clock_that_went_backwards_does_not_make_a_row_from_the_future(self):
        traffic = Traffic()
        traffic.seen(IN, "/channel/0/gain", 0.5, "mixer")
        SeenFile(self.path, traffic).write()

        saved = json.loads(self.path.read_text())
        saved["at"] += 86400.0          # the file claims to be from tomorrow
        self.path.write_text(json.dumps(saved))

        after = Traffic()
        SeenFile(self.path, after).restore()
        snapshot = after.snapshot()
        self.assertGreaterEqual(snapshot["at"] - snapshot["rows"][0]["last_seen"],
                                0.0)

    def test_a_live_row_wins_over_a_saved_one(self):
        """Restoring happens at start-up, but nothing stops a message
        arriving first. What is happening now is never overwritten by what
        happened yesterday."""
        self._saved()
        after = Traffic()
        after.seen(IN, "/channel/0/gain", 0.9, "motion")
        SeenFile(self.path, after).restore()
        row = next(r for r in after.snapshot()["rows"]
                   if r["address"] == "/channel/0/gain")
        self.assertEqual(row["count"], 1)
        self.assertEqual(row["peer"], "motion")


class NothingOnTheHotPath(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.path = Path(self.folder.name) / "seen.json"

    def tearDown(self):
        self.folder.cleanup()

    def test_seeing_a_message_writes_nothing(self):
        """`seen()` runs about a hundred times a second. A file in there is a
        file in the way of the music."""
        traffic = Traffic()
        file = SeenFile(self.path, traffic, delay=60.0)
        file.follow()
        for index in range(500):
            traffic.seen(IN, f"/channel/{index}/gain", 0.5, "mixer")
        self.assertEqual(file.writes, 0)
        self.assertFalse(self.path.exists())
        file.stop()

    def test_a_burst_of_new_addresses_is_one_write(self):
        traffic = Traffic()
        file = SeenFile(self.path, traffic, delay=0.05)
        file.follow()
        try:
            for index in range(200):
                traffic.seen(IN, f"/channel/{index}/gain", 0.5, "mixer")
                time.sleep(0.001)
            self._wait_for(lambda: file.writes >= 1)
            time.sleep(0.2)
            self.assertEqual(file.writes, 1)
        finally:
            file.stop()

    def test_messages_on_an_address_already_known_write_nothing(self):
        """The list is what is kept. A count that ticks up on an address that
        is already in the file is not a new list."""
        traffic = Traffic()
        traffic.seen(IN, "/channel/0/gain", 0.5, "mixer")
        file = SeenFile(self.path, traffic, delay=0.05)
        file.follow()
        try:
            self._wait_for(lambda: file.writes >= 1)
            for _ in range(1000):
                traffic.seen(IN, "/channel/0/gain", 0.5, "mixer")
            time.sleep(0.25)
            self.assertEqual(file.writes, 1)
        finally:
            file.stop()

    def test_a_write_leaves_no_half_file_behind(self):
        traffic = Traffic()
        traffic.seen(IN, "/channel/0/gain", 0.5, "mixer")
        SeenFile(self.path, traffic).write()
        self.assertTrue(self.path.exists())
        self.assertFalse(self.path.with_name(self.path.name + ".new").exists())

    def _wait_for(self, ready, seconds=5.0):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            if ready():
                return
            time.sleep(0.01)
        self.fail("it never happened")


class ABrokenFileIsNotAReasonToFall(unittest.TestCase):
    """The rule a3_core_state.StateFile already lives under, and the one the
    whole window lives under: Core makes the sound."""

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.path = Path(self.folder.name) / "seen.json"

    def tearDown(self):
        self.folder.cleanup()

    def test_a_missing_file_restores_nothing_and_raises_nothing(self):
        traffic = Traffic()
        SeenFile(self.path, traffic).restore()
        self.assertEqual(traffic.snapshot()["rows"], [])

    def test_half_a_file_restores_nothing(self):
        self.path.write_text('{"rows": [["in", "/a", 1')
        traffic = Traffic()
        SeenFile(self.path, traffic).restore()
        self.assertEqual(traffic.snapshot()["rows"], [])

    def test_a_file_of_another_shape_restores_nothing(self):
        self.path.write_text('"a string where an object should be"')
        traffic = Traffic()
        SeenFile(self.path, traffic).restore()
        self.assertEqual(traffic.snapshot()["rows"], [])

    def test_one_bad_row_does_not_take_the_good_ones_with_it(self):
        self.path.write_text(json.dumps({
            "version": 1, "at": time.time(),
            "rows": [["in", "/a", 1, "mixer", time.time()],
                     ["in"],
                     "not a row at all",
                     ["in", "/b", "not a number", "mixer", time.time()],
                     ["out", "/c", 2, "reaper", time.time()]],
            "unknown": []}))
        traffic = Traffic()
        SeenFile(self.path, traffic).restore()
        self.assertEqual(sorted(row["address"]
                                for row in traffic.snapshot()["rows"]),
                         ["/a", "/c"])

    def test_a_directory_that_cannot_be_written_does_not_raise(self):
        traffic = Traffic()
        traffic.seen(IN, "/channel/0/gain", 0.5, "mixer")
        file = SeenFile(Path("/proc/nowhere/seen.json"), traffic)
        file.write()
        self.assertEqual(file.writes, 0)
        self.assertTrue(file.problem)


if __name__ == "__main__":
    unittest.main()
