"""Positions: the newest wins (2026-10-02).

Core reads its port in one thread. Motion sends ~2,300 positions a second;
under extra load the queue filled, a desk command waited behind thousands of
stale positions ("laggy from touch to sound") and packets were dropped. Core
now takes everything waiting and handles, per position word, only the last
one; everything else is handled as it came, in order."""

import socket
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

from pythonosc.osc_bundle_builder import IMMEDIATELY, OscBundleBuilder  # noqa: E402
from pythonosc.osc_message_builder import OscMessageBuilder  # noqa: E402

import a3_osc                                          # noqa: E402
from a3_core_latest import drain, latest_wins, position_key, serve  # noqa: E402

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")
POSITION_WORDS = {"channel.azimuth", "channel.elevation"}


def is_position(address):
    found = TRUTH.match(address)
    return found is not None and found[0] in POSITION_WORDS


def message(address, value):
    builder = OscMessageBuilder(address=address)
    builder.add_arg(value)
    return builder.build()


def packet(address, value=0.5):
    return message(address, value).dgram


def position_bundle(ch, az, el):
    """As Motion sends a position: azimuth and elevation in one bundle."""
    bundle = OscBundleBuilder(IMMEDIATELY)
    bundle.add_content(message(TRUTH.address("channel.azimuth", ch=ch), az))
    bundle.add_content(message(TRUTH.address("channel.elevation", ch=ch), el))
    return bundle.build().dgram


def key(data):
    return position_key(data, is_position)


class WhichPacketIsAPosition(unittest.TestCase):
    def test_motions_bundle_is_a_position(self):
        self.assertIsNotNone(key(position_bundle(1, 10.0, 0.0)))

    def test_two_channels_are_two_keys(self):
        self.assertNotEqual(key(position_bundle(1, 0.0, 0.0)), key(position_bundle(2, 0.0, 0.0)))

    def test_the_same_channel_is_the_same_key(self):
        self.assertEqual(key(position_bundle(3, 1.0, 2.0)), key(position_bundle(3, 5.0, 6.0)))

    def test_a_fader_is_no_position(self):
        self.assertIsNone(key(packet(TRUTH.address("channel.volume", ch=1))))

    def test_a_damaged_packet_is_no_position(self):
        self.assertIsNone(key(b"\x00\x01garbage"))


class TheNewestWins(unittest.TestCase):
    def test_only_the_last_position_per_channel_is_handled(self):
        packets = [(position_bundle(1, float(i), 0.0), ("m", 1)) for i in range(5)]
        kept = latest_wins(packets, key)
        self.assertEqual(kept, [packets[-1]])

    def test_everything_else_is_kept_in_order(self):
        fader = (packet(TRUTH.address("channel.volume", ch=2)), ("d", 1))
        cue = (packet(TRUTH.address("channel.cue", ch=1)), ("d", 1))
        p1 = (position_bundle(1, 1.0, 0.0), ("m", 1))
        p2 = (position_bundle(1, 2.0, 0.0), ("m", 1))
        q = (position_bundle(2, 2.0, 0.0), ("m", 1))
        self.assertEqual(latest_wins([p1, fader, q, p2, cue], key), [fader, q, p2, cue])

    def test_nothing_waiting_is_nothing(self):
        self.assertEqual(latest_wins([], key), [])


class ReadingWhatWaits(unittest.TestCase):
    def setUp(self):
        self.receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.receiver.bind(("127.0.0.1", 0))
        self.sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def tearDown(self):
        self.receiver.close()
        self.sender.close()

    def send(self, n):
        for i in range(n):
            self.sender.sendto(bytes([i]), self.receiver.getsockname())

    def test_it_takes_everything_waiting(self):
        self.send(7)
        got = drain(self.receiver, 100)
        self.assertEqual([data for data, _ in got], [bytes([i]) for i in range(7)])

    def test_it_stops_at_the_limit(self):
        self.send(7)
        self.assertEqual(len(drain(self.receiver, 3)), 3)
        self.assertEqual(len(drain(self.receiver, 100)), 4)

    def test_an_empty_socket_returns_at_once(self):
        self.assertEqual(drain(self.receiver, 100), [])


class TheLoop(unittest.TestCase):
    """serve(): wait, take what waits, newest position wins, hand on."""

    def setUp(self):
        self.receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.receiver.bind(("127.0.0.1", 0))
        self.sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.handled, self.reports = [], []

    def tearDown(self):
        self.receiver.close()
        self.sender.close()

    def run_once(self, handle):
        passes = iter([True, False])
        serve(self.receiver, handle, key, 100, self.reports.append, lambda: next(passes))

    def test_a_burst_is_handled_newest_position_first_class(self):
        fader = packet(TRUTH.address("channel.volume", ch=1))
        for i in range(5):
            self.sender.sendto(position_bundle(1, float(i), 0.0), self.receiver.getsockname())
        self.sender.sendto(fader, self.receiver.getsockname())
        self.run_once(lambda data, client: self.handled.append(data))
        self.assertEqual(self.handled, [position_bundle(1, 4.0, 0.0), fader])

    def test_a_handler_error_is_reported_and_the_rest_still_handled(self):
        def handle(data, client):
            if data == b"bad":
                raise ValueError("boom")
            self.handled.append(data)
        for data in (b"bad", packet(TRUTH.address("channel.volume", ch=2))):
            self.sender.sendto(data, self.receiver.getsockname())
        self.run_once(handle)
        self.assertEqual(len(self.handled), 1)
        self.assertEqual(len(self.reports), 1)


class CoreReadsItsPortThisWay(unittest.TestCase):
    """The main port goes through serve(); the feedback port (REAPER's
    answers, no positions) keeps serve_forever."""

    def setUp(self):
        self.core = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()

    def test_the_main_port_lets_the_newest_position_win(self):
        self.assertIn("serve(server.socket,", self.core)
        self.assertNotIn("    server.serve_forever()", self.core)

    def test_positions_are_the_truths_position_words(self):
        self.assertIn('"channel.azimuth"', self.core)
        self.assertIn('"channel.elevation"', self.core)
