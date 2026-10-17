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

from a3_core_desks import (JOINED, KNOWN, KIND_CHANGED, MAX_DESKS, REFUSED,  # noqa: E402
                           REFUSED_FIRST, DESK, PRODJ, DeskFollowers, SaidKinds, desk_name, kind_of)
from a3_core_prodj_live import ProdjLive   # noqa: E402

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

    def test_a_known_host_saying_another_kind_takes_it_as_a_change(self):
        """Core drops the old client and joins the new one: its name is its
        kind, and relay() and the silence read the name."""
        desks = followers()
        desks.hello("h", 0.0)
        self.assertEqual(desks.hello("h", 1.0, kind=PRODJ), KIND_CHANGED)
        self.assertEqual(desks.name_of("h", PORT), "prodj@h")
        self.assertEqual(desks.hosts_of_kind(DESK), [])
        self.assertEqual(desks.hello("h", 2.0, kind=PRODJ), KNOWN)

    def test_the_same_kind_again_is_known(self):
        desks = followers()
        desks.hello("h", 0.0, kind=PRODJ)
        self.assertEqual(desks.hello("h", 1.0, kind=PRODJ), KNOWN)


class AnUnfollowedProdj(unittest.TestCase):
    """A pro-DJ app without a slot -- the fifth, or one forgotten after a
    minute's silence -- is still a pro-DJ app to the LIVE gate: it is named
    by the kind its host last said in a desk hello, from its desk.osc socket."""

    def full_house(self):
        desks = followers()
        for i in range(MAX_DESKS):
            desks.hello(f"10.0.0.{i}", 0.0)
        return desks

    def gate_name(self, desks, said, host, port):
        return said.gate_name(desks.name_of(host, port) or host, host, port)

    def test_a_fifth_prodjs_volume_is_refused_while_live_is_off(self):
        desks, said = self.full_house(), SaidKinds(PORT)
        self.assertEqual(desks.hello("10.0.0.9", 0.0, kind=PRODJ), REFUSED_FIRST)
        said.said("10.0.0.9", PRODJ)
        who = self.gate_name(desks, said, "10.0.0.9", PORT)
        self.assertEqual(who, "prodj@10.0.0.9")
        self.assertFalse(ProdjLive().admits(who, "channel.volume"))

    def test_a_forgotten_prodj_stays_a_prodj(self):
        desks, said = followers(), SaidKinds(PORT)
        desks.hello("h", 0.0, kind=PRODJ)
        said.said("h", PRODJ)
        desks.silence(1000.0)
        self.assertEqual(self.gate_name(desks, said, "h", PORT), "prodj@h")

    def test_the_last_desk_hello_counts(self):
        said = SaidKinds(PORT)
        said.said("h", PRODJ)
        said.said("h", DESK)
        self.assertEqual(said.gate_name("h", "h", PORT), "desk@h")

    def test_other_hellos_do_not_count(self):
        """a3nuc2 runs Motion beside the app: Motion's hello leaves the kind."""
        said = SaidKinds(PORT)
        said.said("h", PRODJ)
        said.said("h", "motion")
        self.assertEqual(said.gate_name("h", "h", PORT), "prodj@h")

    def test_another_socket_on_that_host_keeps_its_name(self):
        said = SaidKinds(PORT)
        said.said("h", PRODJ)
        self.assertEqual(said.gate_name("motion", "h", 51234), "motion")

    def test_a_host_that_never_said_a_desk_hello_keeps_its_name(self):
        self.assertEqual(SaidKinds(PORT).gate_name("mixer", "h", PORT), "mixer")

    def test_a_followed_desk_keeps_its_name(self):
        said = SaidKinds(PORT)
        said.said("h", PRODJ)
        self.assertEqual(said.gate_name("desk@h", "h", PORT), "desk@h")


if __name__ == "__main__":
    unittest.main()
