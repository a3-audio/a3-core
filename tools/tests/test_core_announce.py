"""Core says where the truth is (spec truth-from-core, 2026-10-02): every
2 s by UDP broadcast, /core/here with the URL and the fingerprint."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

from pythonosc.osc_message import OscMessage   # noqa: E402

import a3_osc                                    # noqa: E402
from a3_core_announce import (announce, announcement,   # noqa: E402
                              broadcast_address, truth_url)

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


class WhereTheBroadcastGoes(unittest.TestCase):
    def test_the_subnets_broadcast_address(self):
        self.assertEqual(broadcast_address({"address": "192.168.8.10/24"}), "192.168.8.255")

    def test_a_single_host_falls_back_to_limited_broadcast(self):
        self.assertEqual(broadcast_address({"address": "192.168.8.10/32"}), "255.255.255.255")
        self.assertEqual(broadcast_address({"address": "192.168.8.10"}), "255.255.255.255")

    def test_a_missing_or_broken_address_falls_back_too(self):
        self.assertEqual(broadcast_address({}), "255.255.255.255")
        self.assertEqual(broadcast_address({"address": "nonsense"}), "255.255.255.255")


class WhatIsSaid(unittest.TestCase):
    def test_the_url_is_cores_window(self):
        self.assertEqual(truth_url(TRUTH),
                         f"http://{TRUTH.host('core')}:{TRUTH.port('core', 'web')}/api/truth")

    def test_the_message_says_url_and_fingerprint(self):
        message = OscMessage(announcement(TRUTH))
        self.assertEqual(message.address, TRUTH.address("core.here"))
        self.assertEqual(message.params, [truth_url(TRUTH), TRUTH.fingerprint()])


class TheLoop(unittest.TestCase):
    def test_a_failed_send_is_reported_and_the_loop_goes_on(self):
        reports, sent = [], []

        class Socket:
            def sendto(self, packet, target):
                if not sent:
                    sent.append("fail")
                    raise OSError("network unreachable")
                sent.append(packet)

        passes = iter([True, True, False])
        announce(Socket(), ("255.255.255.255", 7790), b"x", 2.0, lambda seconds: None,
                 lambda: next(passes), reports.append)
        self.assertEqual(sent, ["fail", b"x"])
        self.assertEqual(len(reports), 1)


class CoreAnnounces(unittest.TestCase):
    def setUp(self):
        self.core = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()

    def test_the_announcement_runs_in_its_own_thread(self):
        self.assertIn('name="a3-announce"', self.core)
        self.assertIn("target=announce", self.core)

    def test_the_window_serves_the_truth(self):
        self.assertIn("truth=lambda: (_truth.canonical(), _truth.fingerprint())", self.core)

    def test_a_refused_network_file_is_said(self):
        self.assertIn("_truth.network_problem", self.core)
