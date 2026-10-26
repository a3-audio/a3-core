# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Deck k listens on stemdeck.osc + (k - 1) (engine plan 4, task B6).

Two one-deck StemDeck instances on one host cannot both bind the truth's
stemdeck.osc port, so deck A takes the base, B the next, and so on. Every
word Core addresses to one deck goes to that port; the base comes from the
truth. Core runs in-process with the UDP client replaced by a recorder that
keeps the (host, port) it was made for.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

import test_core_engine_mode_in_process as in_process
from test_core_engine_mode_in_process import DECK_A, PACKAGE, TRUTH

sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc                                    # noqa: E402
from a3_core_decks import deck_port              # noqa: E402

DECK_B = ("127.0.0.3", 6001)


def truth_with_base(directory, port):
    data = json.loads(Path(TRUTH).read_text())
    for listener in data["listeners"]:
        if listener["name"] == "stemdeck.osc":
            listener["port"] = port
    path = Path(directory) / "a3-osc.json"
    path.write_text(json.dumps(data))
    return a3_osc.load(path)


class Wire:
    """Stands in for SimpleUDPClient: remembers where it points."""
    made = []

    def __init__(self, host, port):
        self.target = (host, port)
        self.sent = []
        Wire.made.append(self)

    def send_message(self, address, value):
        self.sent.append((address, value))


class DeckPortFunction(unittest.TestCase):
    def test_deck_k_is_the_base_plus_k_minus_one(self):
        truth = a3_osc.load(TRUTH)
        base = truth.endpoint("stemdeck", "osc")[1]
        self.assertEqual([deck_port(truth, k) for k in (1, 2, 3, 4)],
                         [base, base + 1, base + 2, base + 3])

    def test_the_base_follows_the_truth(self):
        with tempfile.TemporaryDirectory() as directory:
            truth = truth_with_base(directory, 9000)
            self.assertEqual(deck_port(truth, 1), 9000)
            self.assertEqual(deck_port(truth, 2), 9001)


class DeckPortsInCore(unittest.TestCase):
    # The in-process Core of B4, without its tests.
    _restore = staticmethod(in_process.EngineModeInProcess._restore)
    hello = in_process.EngineModeInProcess.hello
    channel = in_process.EngineModeInProcess.channel

    def setUp(self):
        in_process.EngineModeInProcess.setUp(self)
        Wire.made = []
        self.core["SimpleUDPClient"] = Wire
        self.core["_deck_hosts"].clear()
        self.core["_deck_clients"].clear()
        self.hello("boot-1")
        self.core["say_the_whole_state"]()

    def deck_hello(self, deck, peer):
        self.core["osc_handler_deck_hello"](peer, f"/stemdeck/{deck}/hello", "x")

    def wire_of(self, deck):
        return [w for w in Wire.made
                if w.target[0] == (DECK_A if deck == 1 else DECK_B)[0]][-1]

    def put_deck_on_channel(self, deck, channel):
        for _ in range(deck):
            self.channel(f"/channel/{channel}/stem/turn", 1)
        self.channel(f"/channel/{channel}/stem/push", 1)

    def test_deck_b_3d_goes_to_7781(self):
        self.deck_hello(2, DECK_B)
        self.channel("/channel/3/3d", 0.4)
        self.put_deck_on_channel(2, 3)
        wire = self.wire_of(2)
        self.assertEqual(wire.target, (DECK_B[0], 7781))
        self.assertIn(("/stemdeck/2/3d", 0.4), wire.sent)
        self.channel("/channel/3/3d", 0.9)
        self.assertEqual(wire.sent[-1], ("/stemdeck/2/3d", 0.9))

    def test_deck_a_stays_on_the_base_port(self):
        self.deck_hello(1, DECK_A)
        self.assertEqual(self.wire_of(1).target, (DECK_A[0], 7780))

    def test_the_port_follows_the_truths_base(self):
        with tempfile.TemporaryDirectory() as directory:
            self.core["_truth"] = truth_with_base(directory, 9000)
            self.deck_hello(1, DECK_A)
            self.deck_hello(2, DECK_B)
        self.assertEqual(self.wire_of(1).target[1], 9000)
        self.assertEqual(self.wire_of(2).target[1], 9001)

    def test_the_hello_reply_goes_to_the_decks_port(self):
        self.deck_hello(2, DECK_B)
        wire = self.wire_of(2)
        self.assertEqual(wire.target[1], 7781)
        self.assertEqual(wire.sent, [("/stemdeck/2/3d", 1.0)])

    def test_the_selector_arrival_goes_to_the_decks_port(self):
        self.deck_hello(2, DECK_B)
        self.put_deck_on_channel(2, 1)
        wire = self.wire_of(2)
        self.assertEqual(wire.target[1], 7781)
        self.assertTrue(wire.sent)

    def test_the_leave_neutral_goes_to_the_decks_port(self):
        self.deck_hello(2, DECK_B)
        self.put_deck_on_channel(2, 1)
        wire = self.wire_of(2)
        wire.sent.clear()
        self.channel("/channel/1/stem/turn", -1)    # back to ANALOG
        self.channel("/channel/1/stem/push", 1)
        self.assertEqual(wire.target[1], 7781)
        self.assertEqual(wire.sent, [("/stemdeck/2/3d", 1.0)])

    def test_both_decks_on_one_host_3d_goes_to_their_own_ports(self):
        same = ("127.0.0.2", 6002)
        self.deck_hello(1, DECK_A)
        self.deck_hello(2, same)
        self.channel("/channel/1/3d", 0.2)
        self.channel("/channel/2/3d", 0.3)
        self.put_deck_on_channel(1, 1)
        self.put_deck_on_channel(2, 2)
        by_port = {w.target[1]: w for w in Wire.made if w.target[0] == DECK_A[0]}
        self.assertEqual(sorted(by_port), [7780, 7781])
        self.assertIn(("/stemdeck/1/3d", 0.2), by_port[7780].sent)
        self.assertIn(("/stemdeck/2/3d", 0.3), by_port[7781].sent)
        self.assertNotIn(("/stemdeck/2/3d", 0.3), by_port[7780].sent)

    def test_engine_mode_prodj_live_reaches_every_deck_once(self):
        self.core["stemdeck_said_hello"](DECK_A[0], 1.0)
        self.deck_hello(1, DECK_A)
        self.deck_hello(2, ("127.0.0.2", 6002))
        targets = []
        for w in Wire.made:
            w.sent.clear()
        self.core["announce_live"]()
        for w in Wire.made:
            targets += [w.target[1]] * sum(1 for a, _ in w.sent if a == "/prodj/live")
        self.assertEqual(sorted(targets), [7780, 7781])

    def test_reaper_mode_prodj_live_goes_to_the_base_client_only(self):
        self.core["_engine_mode"] = False
        self.core["stemdeck_said_hello"](DECK_A[0], 1.0)
        self.core["_deck_clients"][2] = self.core["WatchedClient"](
            Wire("127.0.0.2", 7781), "stemdeck-B")
        for w in Wire.made:
            w.sent.clear()
        self.core["announce_live"]()
        got = [w.target[1] for w in Wire.made
               if any(a == "/prodj/live" for a, _ in w.sent)]
        self.assertEqual(got, [7780])


if __name__ == "__main__":
    unittest.main()
