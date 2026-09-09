"""The window onto Core's traffic.

Two things are worth testing here and one is not. The rate arithmetic and the
JSON shaping are pure functions over a snapshot, so they get proper tests. The
HTTP plumbing is thin -- a handler that calls one of those and writes the
result -- so it gets one integration test that binds to port 0 and asks for a
page. What is deliberately not tested is the browser: that is what the smoke
test is for.
"""

import json
import sys
import unittest
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = (ROOT / "platform-config/debian-x86_64/a3-core"
           / "home/aaa/.local")
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_traffic import IN, OUT, Traffic   # noqa: E402
from a3_core_web import as_json, start_window   # noqa: E402


class TheRateComesFromTwoSnapshots(unittest.TestCase):
    """The writer only counts. This is where a rate is made."""

    def test_ten_messages_in_two_seconds_is_five_a_second(self):
        first = {"at": 100.0,
                 "rows": [{"direction": IN, "address": "/a", "count": 5,
                           "last_value": 1.0, "last_type": "float",
                           "last_seen": 100.0, "peer": "motion"}],
                 "history": [], "unhandled": {}}
        second = {"at": 102.0,
                  "rows": [{"direction": IN, "address": "/a", "count": 15,
                            "last_value": 1.0, "last_type": "float",
                            "last_seen": 102.0, "peer": "motion"}],
                  "history": [], "unhandled": {}}

        rows = as_json(second, first)["rows"]
        self.assertAlmostEqual(rows[0]["rate"], 5.0)

    def test_without_a_previous_snapshot_there_is_no_rate(self):
        """Not zero. Zero would read as "nothing is arriving", which is a
        different and much more alarming statement than "not known yet"."""
        snapshot = {"at": 100.0,
                    "rows": [{"direction": IN, "address": "/a", "count": 5,
                              "last_value": 1.0, "last_type": "float",
                              "last_seen": 100.0, "peer": "motion"}],
                    "history": [], "unhandled": {}}
        self.assertIsNone(as_json(snapshot, None)["rows"][0]["rate"])

    def test_a_row_that_is_new_since_the_last_look_has_no_rate(self):
        first = {"at": 100.0, "rows": [], "history": [], "unhandled": {}}
        second = {"at": 101.0,
                  "rows": [{"direction": IN, "address": "/b", "count": 3,
                            "last_value": 1.0, "last_type": "float",
                            "last_seen": 101.0, "peer": "motion"}],
                  "history": [], "unhandled": {}}
        self.assertIsNone(as_json(second, first)["rows"][0]["rate"])

    def test_two_snapshots_at_the_same_instant_do_not_divide_by_zero(self):
        same = {"at": 100.0,
                "rows": [{"direction": IN, "address": "/a", "count": 5,
                          "last_value": 1.0, "last_type": "float",
                          "last_seen": 100.0, "peer": "motion"}],
                "history": [], "unhandled": {}}
        self.assertIsNone(as_json(same, dict(same))["rows"][0]["rate"])

    def test_the_two_directions_of_one_address_get_their_own_rates(self):
        first = {"at": 100.0,
                 "rows": [{"direction": IN, "address": "/a", "count": 0,
                           "last_value": 1.0, "last_type": "float",
                           "last_seen": 100.0, "peer": "motion"},
                          {"direction": OUT, "address": "/a", "count": 0,
                           "last_value": 1.0, "last_type": "float",
                           "last_seen": 100.0, "peer": "reaper"}],
                 "history": [], "unhandled": {}}
        second = {"at": 101.0,
                  "rows": [{"direction": IN, "address": "/a", "count": 4,
                            "last_value": 1.0, "last_type": "float",
                            "last_seen": 101.0, "peer": "motion"},
                           {"direction": OUT, "address": "/a", "count": 1,
                            "last_value": 1.0, "last_type": "float",
                            "last_seen": 101.0, "peer": "reaper"}],
                  "history": [], "unhandled": {}}
        rows = {(r["direction"], r["address"]): r
                for r in as_json(second, first)["rows"]}
        self.assertAlmostEqual(rows[(IN, "/a")]["rate"], 4.0)
        self.assertAlmostEqual(rows[(OUT, "/a")]["rate"], 1.0)


class EverythingSurvivesJsonDumps(unittest.TestCase):
    def test_a_snapshot_of_real_traffic_serialises(self):
        traffic = Traffic()
        traffic.seen(IN, "/channel/0/pfl", "1", "mixer")
        traffic.seen(IN, "/channel/0/pfl", 1.0, "motion")
        traffic.seen(OUT, "/track/4/mute", 0.0, "reaper")
        traffic.unhandled("/track/3/name")

        text = json.dumps(as_json(traffic.snapshot(), None))
        self.assertIn("/channel/0/pfl", text)

    def test_a_value_json_cannot_hold_becomes_its_repr(self):
        """OSC can carry blobs. The window must not fall over on one."""
        traffic = Traffic()
        traffic.seen(IN, "/blob", b"\x00\x01", "motion")
        row = as_json(traffic.snapshot(), None)["rows"][0]
        self.assertIsInstance(row["last_value"], str)
        self.assertEqual(row["last_type"], "bytes")
        json.dumps(row)


class TheServerAnswers(unittest.TestCase):
    def setUp(self):
        self.traffic = Traffic()
        self.traffic.seen(IN, "/channel/0/gain", 0.5, "motion")
        self.listening = start_window(self.traffic, "127.0.0.1:0")
        self.assertTrue(self.listening, "the window did not bind")

    def tearDown(self):
        from a3_core_web import stop_window
        stop_window()

    def url(self, path):
        from a3_core_web import window_address
        host, port = window_address()
        return f"http://{host}:{port}{path}"

    def test_the_snapshot_is_json_and_has_the_row(self):
        with urllib.request.urlopen(self.url("/api/traffic"), timeout=5) as r:
            payload = json.loads(r.read())
        self.assertEqual(payload["rows"][0]["address"], "/channel/0/gain")

    def test_the_page_is_served(self):
        with urllib.request.urlopen(self.url("/"), timeout=5) as r:
            body = r.read().decode()
        self.assertIn("<table", body.lower())

    def test_an_unknown_path_is_a_404_and_not_a_crash(self):
        import urllib.error
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(self.url("/nope"), timeout=5)
        self.assertEqual(caught.exception.code, 404)


class ABusyPortDoesNotTakeCoreDown(unittest.TestCase):
    def test_a_port_already_in_use_returns_false_rather_than_raising(self):
        """The rule this file exists under: the window never stops Core."""
        import socket
        from a3_core_web import stop_window

        holder = socket.socket()
        holder.bind(("127.0.0.1", 0))
        holder.listen(1)
        taken = holder.getsockname()[1]
        try:
            self.assertFalse(start_window(Traffic(),
                                          f"127.0.0.1:{taken}"))
        finally:
            stop_window()
            holder.close()


class ABadBindDoesNotTakeCoreDown(unittest.TestCase):
    """Same rule as the busy port -- an operator's typo, not a crash."""

    def test_a_port_above_the_range_returns_false_rather_than_raising(self):
        """Rejected twice over, deliberately. `_address_from`'s range check
        catches this before any socket exists -- that is the layer that
        actually runs. Behind it, `start_window`'s `except` still names
        `OverflowError`, the one `socket.bind()` itself would raise for this
        input; that arm is what would catch it if the range check were ever
        "simplified" away. Both are kept on purpose: an extra digit must not
        propagate into Core's startup, from either layer."""
        from a3_core_web import stop_window
        try:
            self.assertFalse(start_window(Traffic(), "127.0.0.1:99080"))
        finally:
            stop_window()

    def test_a_bind_with_no_colon_returns_false_and_binds_nothing(self):
        """"Returns False" and "did not quietly bind the world" are two
        different claims, and only the second is the point: an empty host
        from a missing colon means INADDR_ANY, which is the one thing the
        localhost default exists to prevent."""
        import socket
        from a3_core_web import stop_window
        try:
            self.assertFalse(start_window(Traffic(), "9080"))
            with self.assertRaises(OSError):
                socket.create_connection(("127.0.0.1", 9080), timeout=0.2)
        finally:
            stop_window()

    def test_a_colon_with_no_host_is_refused_too(self):
        """The other way to end up binding every interface.

        `'9080'` is caught by the missing colon, so it never reaches the
        empty-host check -- and without a case that does reach it, deleting
        that check would leave the suite green while `':9080'` bound
        INADDR_ANY again. That is the defect this test exists to keep dead,
        not the return value.
        """
        import socket
        from a3_core_web import stop_window
        try:
            self.assertFalse(start_window(Traffic(), ":9080"))
            with self.assertRaises(OSError):
                socket.create_connection(("127.0.0.1", 9080), timeout=0.2)
        finally:
            stop_window()

    def test_a_non_numeric_port_returns_false_rather_than_raising(self):
        from a3_core_web import stop_window
        try:
            self.assertFalse(start_window(Traffic(), "127.0.0.1:not-a-port"))
        finally:
            stop_window()


if __name__ == "__main__":
    unittest.main()
