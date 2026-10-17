# SPDX-FileCopyrightText: 2026 Patric Schmitz, Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Core follows every virtual desk that says hello, four at most (spec
virtual-desk, decided 2026-10-16), and names each by its listening socket."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_desks import (JOINED, KNOWN, MAX_DESKS, REFUSED, REFUSED_FIRST,  # noqa: E402
                           DESK, PRODJ, DeskFollowers, desk_name, kind_of)

PORT, VU = 7773, 7774


def followers():
    return DeskFollowers(PORT, VU)


class AHello(unittest.TestCase):
    def test_the_first_joins(self):
        self.assertEqual(followers().hello("192.168.8.20", 0.0), JOINED)

    def test_the_next_from_the_same_host_is_known(self):
        desks = followers()
        desks.hello("192.168.8.20", 0.0)
        self.assertEqual(desks.hello("192.168.8.20", 30.0), KNOWN)

    def test_four_join_side_by_side(self):
        desks = followers()
        verdicts = [desks.hello(f"192.168.8.{20 + i}", 0.0) for i in range(MAX_DESKS)]
        self.assertEqual(verdicts, [JOINED] * MAX_DESKS)
        self.assertEqual(len(desks.hosts()), MAX_DESKS)

    def test_a_fifth_is_refused_and_said_once(self):
        desks = followers()
        for i in range(MAX_DESKS):
            desks.hello(f"192.168.8.{20 + i}", 0.0)
        self.assertEqual(desks.hello("192.168.8.99", 1.0), REFUSED_FIRST)
        self.assertEqual(desks.hello("192.168.8.99", 31.0), REFUSED)
        self.assertNotIn("192.168.8.99", desks.hosts())


class Silence(unittest.TestCase):
    def test_a_minute_without_hello_forgets_the_desk_once(self):
        desks = followers()
        desks.hello("192.168.8.20", 0.0)
        self.assertEqual(desks.silence(59.0), [])
        self.assertEqual(desks.silence(61.0), ["192.168.8.20"])
        self.assertEqual(desks.silence(90.0), [])
        self.assertEqual(desks.hosts(), [])

    def test_a_desk_that_comes_back_joins_again(self):
        desks = followers()
        desks.hello("192.168.8.20", 0.0)
        desks.silence(61.0)
        self.assertEqual(desks.hello("192.168.8.20", 62.0), JOINED)

    def test_a_refused_desk_gets_the_slot_that_frees(self):
        desks = followers()
        for i in range(MAX_DESKS):
            desks.hello(f"192.168.8.{20 + i}", 0.0)
        desks.hello("192.168.8.99", 1.0)
        for i in range(1, MAX_DESKS):
            desks.hello(f"192.168.8.{20 + i}", 50.0)
        desks.silence(61.0)  # .20 is gone
        self.assertEqual(desks.hello("192.168.8.99", 62.0), JOINED)


class Names(unittest.TestCase):
    def test_a_desk_is_named_by_its_listening_socket(self):
        desks = followers()
        desks.hello("192.168.8.20", 0.0)
        self.assertEqual(desks.name_of("192.168.8.20", PORT), desk_name("192.168.8.20"))
        self.assertEqual(desk_name("192.168.8.20"), "desk@192.168.8.20")

    def test_motion_on_the_same_host_is_not_the_desk(self):
        """Review focus 1: a3nuc2 runs Motion and a desk. Motion sends from
        an ephemeral port and must stay "motion", or relay() would leave it
        out of the desk's moves."""
        desks = followers()
        desks.hello("192.168.8.20", 0.0)
        self.assertIsNone(desks.name_of("192.168.8.20", 51234))

    def test_an_unfollowed_host_has_no_desk_name(self):
        self.assertIsNone(followers().name_of("192.168.8.20", PORT))


class Meters(unittest.TestCase):
    def test_every_followed_desk_gets_them_on_its_vu_port(self):
        desks = followers()
        desks.hello("192.168.8.20", 0.0)
        desks.hello("192.168.8.30", 0.0)
        self.assertEqual(desks.vu_destinations(), [("192.168.8.20", VU), ("192.168.8.30", VU)])


if __name__ == "__main__":
    unittest.main()


class Kinds(unittest.TestCase):
    def test_a_prodj_is_named_by_its_kind(self):
        desks = followers()
        desks.hello("h", 0.0, kind=PRODJ)
        self.assertEqual(desks.name_of("h", PORT), "prodj@h")

    def test_the_default_kind_is_desk(self):
        desks = followers()
        desks.hello("h", 0.0)
        self.assertEqual(desks.name_of("h", PORT), desk_name("h"))
        self.assertEqual(desk_name("h"), "desk@h")

    def test_hosts_of_kind(self):
        desks = followers()
        desks.hello("a", 0.0)
        desks.hello("b", 0.0, kind=PRODJ)
        self.assertEqual(desks.hosts_of_kind(PRODJ), ["b"])
        self.assertEqual(desks.hosts_of_kind(DESK), ["a"])

    def test_kind_of_a_name(self):
        self.assertEqual(kind_of("prodj@h"), "prodj")
        self.assertEqual(kind_of("desk@h"), "desk")
        self.assertIsNone(kind_of("motion"))

    def test_a_known_host_saying_another_kind_takes_it_and_stays_known(self):
        desks = followers()
        desks.hello("h", 0.0)
        self.assertEqual(desks.hello("h", 1.0, kind=PRODJ), KNOWN)
        self.assertEqual(desks.name_of("h", PORT), "prodj@h")
        self.assertEqual(desks.hosts_of_kind(DESK), [])
