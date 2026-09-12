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
                                 everyone_but,
                                 parse_subscriber,
                                 parse_subscribers,
                                 relay_on_arrival)


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


class Named:
    """Stands in for a WatchedClient, which is all `everyone_but` reads."""

    def __init__(self, name):
        self.name = name


class WhoHearsAValueThatJustArrived(unittest.TestCase):
    """The desk turns a knob; every *other* screen has to show it.

    Not the desk itself: it is holding the knob. And never desk-to-screen
    directly -- Core is the only place that knows who is listening, which is
    the whole point of the subscriber list.
    """

    def setUp(self):
        self.everyone = [Named("mixer"), Named("motion"), Named("light")]

    def names(self, origin):
        return [client.name for client in everyone_but(self.everyone, origin)]

    def test_the_sender_is_left_out(self):
        self.assertEqual(self.names("mixer"), ["motion", "light"])

    def test_a_screen_that_sent_it_is_left_out_too(self):
        """Symmetric on purpose: Motion's own strip is a mixer like any
        other."""
        self.assertEqual(self.names("motion"), ["mixer", "light"])

    def test_with_no_known_sender_everybody_hears_it(self):
        """REAPER's report and a recall have no subscriber behind them, and
        an unnamed host is an unknown one -- not a reason to keep a screen
        dark."""
        for origin in (None, "", "reaper", "127.0.0.1 (mehrdeutig)"):
            with self.subTest(origin=origin):
                self.assertEqual(self.names(origin),
                                 ["mixer", "motion", "light"])

    def test_the_order_is_the_subscriber_order(self):
        """Same reason SHIPPED is ordered: a replay reads the same twice."""
        self.assertEqual(self.names("light"), ["mixer", "motion"])


class WhatIsPassedOnWhenItArrives(unittest.TestCase):
    """Why on arrival at all: REAPER does not report a change back to the
    surface that caused it, and Core *is* that surface. So a knob on the desk
    would otherwise reach REAPER and no screen at all -- measured at the rig
    on 2026-09-12, see
    issues/a3-core-was-am-pult-gedreht-wird-erreicht-motion-nicht.md.
    """

    def test_the_channel_strip_is_passed_on(self):
        for parameter in ("gain", "volume", "fx-send", "3d"):
            with self.subTest(parameter=parameter):
                self.assertTrue(relay_on_arrival(f"/channel/0/{parameter}"))

    def test_the_eq_bands_are_passed_on(self):
        """Four segments rather than three, and the band is not the
        parameter."""
        for band in ("high", "mid", "low"):
            with self.subTest(band=band):
                self.assertTrue(relay_on_arrival(f"/channel/2/eq/{band}"))

    def test_the_master_and_the_filter_are_passed_on(self):
        for address in ("/master/volume", "/master/booth", "/master/phones_mix",
                        "/master/phones_volume", "/master/return",
                        "/fx/frequency", "/fx/resonance"):
            with self.subTest(address=address):
                self.assertTrue(relay_on_arrival(address))

    def test_a_flag_is_not_passed_on_here(self):
        """`pfl`, `fx` and the filter mode are announced by announce_flag()
        and the mode branch -- to everybody, the sender included, because a
        lamp is status and the desk's own lamp has to follow its own key.
        Passing them on here as well would send each twice."""
        for address in ("/channel/0/pfl", "/channel/3/fx", "/fx/mode"):
            with self.subTest(address=address):
                self.assertFalse(relay_on_arrival(address))

    def test_the_position_is_not_passed_on(self):
        """It is not a REAPER parameter -- it goes to the IEM encoders -- and
        it arrives tens of thousands of times per channel. Motion asks for it
        at start-up instead; see
        smoke-test/smoke-test-motion-hoert-die-position.md."""
        for address in ("/channel/0/azimuth", "/channel/1/elevation"):
            with self.subTest(address=address):
                self.assertFalse(relay_on_arrival(address))

    def test_an_accent_still_reaches_the_other_screens(self):
        """pot_1 and pot_2 carry Motion's *effective* value, accent included,
        and they are passed on anyway -- because the sender is left out, so
        nothing writes an accent peak into the base it came from. That was the
        ratchet of 2026-09-12 morning, and it lived in the other direction:
        REAPER's report, not arrival."""
        self.assertTrue(relay_on_arrival("/channel/0/pot_1"))
        self.assertTrue(relay_on_arrival("/channel/0/pot_2"))

    def test_what_is_not_a_value_at_all(self):
        for address in ("/beat", "/tap", "/state/recall", "/channel/0",
                        "/channel", "/track/12/fx/1/fxparam/1/value", "/"):
            with self.subTest(address=address):
                self.assertFalse(relay_on_arrival(address))


class TheTwoThatShip(unittest.TestCase):
    def test_the_mixer_comes_before_motion(self):
        """Only so that a replay is reproducible; nothing depends on which
        hears first."""
        self.assertEqual(SHIPPED, ("mixer", "motion"))


if __name__ == "__main__":
    unittest.main()
