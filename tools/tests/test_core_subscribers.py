"""Adding a department without touching the source.

A light or video desk that wants the A3 state is a command-line argument:
`--subscriber light=192.168.43.60:7771`. This is the parsing, and it refuses
rather than defaults -- a subscriber that cannot be understood is a department
that hears nothing all evening, and that failure is invisible over UDP. It is
the same failure, in the same place, that cost three days in September.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_subscribers import (SHIPPED, SubscriberError,   # noqa: E402
                                 parse_subscriber,
                                 parse_subscribers)


class OneSubscriber(unittest.TestCase):
    def test_it_reads_a_name_a_host_and_a_port(self):
        self.assertEqual(parse_subscriber("light=192.168.43.60:7771"),
                         ("light", "192.168.43.60", 7771))

    def test_space_around_the_name_is_forgiven(self):
        """A shell quoting accident, not an opinion."""
        self.assertEqual(parse_subscriber(" video = 127.0.0.1:9100 "),
                         ("video", "127.0.0.1", 9100))

    def test_an_ipv6_host_keeps_its_colons(self):
        """The port is the last colon-separated piece, not the second."""
        name, host, port = parse_subscriber("light=::1:7771")
        self.assertEqual((name, host, port), ("light", "::1", 7771))


class WhatIsRefused(unittest.TestCase):
    """Every one of these would otherwise be a department that hears nothing
    and has no way of finding out."""

    def refuses(self, text):
        with self.assertRaises(SubscriberError, msg=text):
            parse_subscriber(text)

    def test_no_equals_sign(self):
        self.refuses("192.168.43.60:7771")

    def test_no_name(self):
        self.refuses("=192.168.43.60:7771")

    def test_no_port(self):
        self.refuses("light=192.168.43.60")

    def test_no_host(self):
        self.refuses("light=:7771")

    def test_a_port_that_is_not_a_number(self):
        self.refuses("light=192.168.43.60:seven")

    def test_a_port_outside_the_range(self):
        self.refuses("light=192.168.43.60:0")
        self.refuses("light=192.168.43.60:99999")


class SeveralSubscribers(unittest.TestCase):
    def test_they_keep_the_order_they_were_given(self):
        """A recall replays in this order, so the same evening replays the
        same way twice."""
        found = parse_subscribers(["light=10.0.0.1:1", "video=10.0.0.2:2"])
        self.assertEqual([name for name, _, _ in found], ["light", "video"])

    def test_a_name_cannot_be_used_twice(self):
        with self.assertRaises(SubscriberError):
            parse_subscribers(["light=10.0.0.1:1", "light=10.0.0.2:2"])

    def test_a_reserved_name_is_refused(self):
        """A second `motion` would double every message to it and make the
        window's peer column ambiguous; a subscriber called `reaper` would be
        named after a client it is not."""
        for name in SHIPPED + ("reaper", "iem", "dualdelay"):
            with self.subTest(name=name):
                with self.assertRaises(SubscriberError):
                    parse_subscribers(
                        [f"{name}=10.0.0.1:1"],
                        reserved=SHIPPED + ("reaper", "iem", "dualdelay"))

    def test_nothing_given_is_nothing_added(self):
        self.assertEqual(parse_subscribers([]), [])


class TheTwoThatShip(unittest.TestCase):
    def test_the_mixer_comes_before_motion(self):
        """Only so that a replay is reproducible; nothing depends on which
        hears first."""
        self.assertEqual(SHIPPED, ("mixer", "motion"))


if __name__ == "__main__":
    unittest.main()
